#include "ControlEventHandler.h"

#include "../input/InputInjector.h"

#include <QThread>
#include <QTimer>
#include <QMutexLocker>
#include <QDebug>

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

ControlEventHandler::ControlEventHandler(InputInjector* injector,
    QObject* parent)
    : QObject(parent)
    , m_injector(injector)
{
    Q_ASSERT(m_injector);

    // -----------------------------------------------------------------------
    // Move the injector and drain timer onto a dedicated worker thread so
    // Win32 SendInput calls never block the socket event thread.
    // -----------------------------------------------------------------------
    m_workerThread = new QThread(this);
    m_workerThread->setObjectName(QStringLiteral("ControlEventThread"));

    // The injector was created by the caller; move it to our worker thread.
    m_injector->moveToThread(m_workerThread);

    // Create the drain timer on the worker thread by building it after the
    // thread starts (via a queued lambda).
    QObject::connect(m_workerThread, &QThread::started, this, [this]() {
        m_drainTimer = new QTimer();
        m_drainTimer->setInterval(k_drainIntervalMs);
        m_drainTimer->setTimerType(Qt::PreciseTimer);
        QObject::connect(m_drainTimer, &QTimer::timeout,
            this, &ControlEventHandler::drainQueue,
            Qt::DirectConnection);
        m_drainTimer->start();
        }, Qt::QueuedConnection);

    // Clean up the timer when the thread finishes.
    QObject::connect(m_workerThread, &QThread::finished, this, [this]() {
        if (m_drainTimer) {
            m_drainTimer->stop();
            delete m_drainTimer;
            m_drainTimer = nullptr;
        }
        }, Qt::DirectConnection);

    m_workerThread->start();
}

ControlEventHandler::~ControlEventHandler()
{
    m_workerThread->quit();
    if (!m_workerThread->wait(1000)) {
        m_workerThread->terminate();
        m_workerThread->wait(300);
    }
}

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

void ControlEventHandler::setEnabled(bool enabled)
{
    QMutexLocker lock(&m_configMutex);
    m_enabled = enabled;
}

void ControlEventHandler::setActiveRoom(const QString& roomId,
    bool deviceControlEnabled)
{
    QMutexLocker lock(&m_configMutex);
    m_activeRoomId = roomId;
    m_deviceControlEnabled = deviceControlEnabled;
}

void ControlEventHandler::addPeer(const QString& peerId)
{
    QMutexLocker lock(&m_configMutex);
    m_peers.insert(peerId, true);
}

void ControlEventHandler::removePeer(const QString& peerId)
{
    QMutexLocker lock(&m_configMutex);
    m_peers.remove(peerId);
}

// ---------------------------------------------------------------------------
// Public event entry points  (called from socket / signaling thread)
// ---------------------------------------------------------------------------

void ControlEventHandler::handleControl(const nlohmann::json& data)
{
    QString senderId;
    if (!validate(data, senderId)) { return; }

    ControlEvent ev;
    ev.type = ControlEvent::Type::Control;
    ev.senderId = senderId;
    ev.command = data.contains("command") && data["command"].is_string()
        ? QString::fromStdString(data["command"].get<std::string>())
        : QStringLiteral("unknown");

    QMutexLocker lock(&m_queueMutex);
    m_queue.enqueue(std::move(ev));
}

void ControlEventHandler::handleKeyboard(const nlohmann::json& data)
{
    QString senderId;
    if (!validate(data, senderId)) { return; }

    ControlEvent ev;
    ev.type = ControlEvent::Type::Keyboard;
    ev.senderId = senderId;
    ev.key = data.contains("key") && data["key"].is_string()
        ? QString::fromStdString(data["key"].get<std::string>())
        : QString();
    // "eventType" carries the actual sub-type ("keydown" / "keyup" / "keypress").
    // "type" is the dispatch category ("keyboard") – do NOT use it here.
    ev.eventType = data.contains("eventType") && data["eventType"].is_string()
        ? QString::fromStdString(data["eventType"].get<std::string>())
        : QStringLiteral("keydown");
    if (data.contains("modifiers") && data["modifiers"].is_array()) {
        for (const auto& mod : data["modifiers"]) {
            if (mod.is_string()) {
                ev.modifiers << QString::fromStdString(mod.get<std::string>());
            }
        }
    }

    QMutexLocker lock(&m_queueMutex);
    m_queue.enqueue(std::move(ev));
}

void ControlEventHandler::handleMouse(const nlohmann::json& data)
{
    QString senderId;
    if (!validate(data, senderId)) { return; }

    ControlEvent ev;
    ev.type = ControlEvent::Type::Mouse;
    ev.senderId = senderId;
    ev.x = data.contains("x") ? static_cast<float>(data["x"].get<double>()) : 0.0f;
    ev.y = data.contains("y") ? static_cast<float>(data["y"].get<double>()) : 0.0f;
    ev.deltaX = data.contains("deltaX") ? static_cast<float>(data["deltaX"].get<double>()) : 0.0f;
    ev.deltaY = data.contains("deltaY") ? static_cast<float>(data["deltaY"].get<double>()) : 0.0f;
    // "eventType" carries the actual sub-type ("mousemove", "mousedown", "mouseup", etc.).
    // "type" is the dispatch category ("mouse") – do NOT use it here.
    ev.eventType = data.contains("eventType") && data["eventType"].is_string()
        ? QString::fromStdString(data["eventType"].get<std::string>())
        : QStringLiteral("mousemove");
    ev.button = data.contains("button") && data["button"].is_string()
        ? QString::fromStdString(data["button"].get<std::string>())
        : QStringLiteral("left");

    QMutexLocker lock(&m_queueMutex);
    m_queue.enqueue(std::move(ev));
}

// ---------------------------------------------------------------------------
// Drain slot  (runs on m_workerThread at 60 Hz)
// ---------------------------------------------------------------------------

void ControlEventHandler::drainQueue()
{
    // Batch-dequeue all pending events to minimise lock contention.
    QQueue<ControlEvent> batch;
    {
        QMutexLocker lock(&m_queueMutex);
        batch.swap(m_queue);
    }

    while (!batch.isEmpty()) {
        ControlEvent ev = batch.dequeue();

        switch (ev.type) {
        case ControlEvent::Type::Keyboard:
            dispatchKeyboard(ev);
            break;
        case ControlEvent::Type::Mouse:
            dispatchMouse(ev);
            break;
        case ControlEvent::Type::Control:
            dispatchControl(ev);
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// Dispatch helpers  (called on worker thread)
// ---------------------------------------------------------------------------

void ControlEventHandler::dispatchKeyboard(const ControlEvent& ev)
{
    SenderState& state = m_senderState[ev.senderId];
    if (!checkRate(state.keyboard, k_maxKeyboardHz, ev.senderId)) {
        return;
    }
    if (!ev.key.isEmpty()) {
        m_injector->handleKeyboard(ev.key, ev.modifiers, ev.eventType);
    }
}

void ControlEventHandler::dispatchMouse(const ControlEvent& ev)
{
    SenderState& state = m_senderState[ev.senderId];
    if (!checkRate(state.mouse, k_maxMouseHz, ev.senderId)) {
        return;
    }
    m_injector->handleMouse(ev.x, ev.y, ev.eventType, ev.button, ev.deltaX, ev.deltaY);
}

void ControlEventHandler::dispatchControl(const ControlEvent& ev)
{
    qDebug() << "[ControlEventHandler] control command:" << ev.command
        << "from" << ev.senderId;
    Q_EMIT controlReceived(ev.senderId, ev.command);
}

// ---------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------

bool ControlEventHandler::validate(const nlohmann::json& data,
    QString& outSenderId) const
{
    QMutexLocker lock(&m_configMutex);

    if (!m_enabled || !m_deviceControlEnabled) {
        return false;
    }

    if (!data.contains("senderId") || !data["senderId"].is_string()) {
        return false;
    }

    outSenderId = QString::fromStdString(data["senderId"].get<std::string>());

    if (!m_peers.contains(outSenderId)) {
        qDebug() << "[ControlEventHandler] rejected event from unknown peer:"
            << outSenderId;
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// Rate limiting
// ---------------------------------------------------------------------------

bool ControlEventHandler::checkRate(RateCounter& counter,
    int maxPerSec,
    const QString& senderId)
{
    // Reset the counter every full second.
    if (!counter.window.isValid() || counter.window.elapsed() >= 1000) {
        counter.count = 0;
        counter.window.restart();
    }

    if (counter.count >= maxPerSec) {
        emit rateLimitExceeded(senderId);
        return false;
    }

    ++counter.count;
    return true;
}
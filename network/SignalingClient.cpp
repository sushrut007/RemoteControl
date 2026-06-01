#include "SignalingClient.h"

#include <QWebSocket>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrlQuery>
#include <QAbstractSocket>

#include <cmath>
#include <stdexcept>

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

static constexpr char k_eioVersion[] = "4";
static constexpr char k_socketIoPath[] = "/socket.io/";

static constexpr QChar EIO_OPEN = u'0';
static constexpr QChar EIO_CLOSE = u'1';
static constexpr QChar EIO_PING = u'2';
static constexpr QChar EIO_PONG = u'3';
static constexpr QChar EIO_MESSAGE = u'4';
static constexpr QChar EIO_UPGRADE = u'5';

static constexpr QChar SIO_CONNECT = u'0';
static constexpr QChar SIO_DISCONNECT = u'1';
static constexpr QChar SIO_EVENT = u'2';
static constexpr QChar SIO_ACK = u'3';
static constexpr QChar SIO_CONNECT_ERROR = u'4';

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

SignalingClient::SignalingClient(QObject* parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
    , m_ws(new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this))
    , m_pingTimer(new QTimer(this))
    , m_reconnectTimer(new QTimer(this))
{
    m_pingTimer->setSingleShot(false);
    QObject::connect(m_pingTimer, &QTimer::timeout,
        this, &SignalingClient::onPingTimer);

    m_reconnectTimer->setSingleShot(true);
    QObject::connect(m_reconnectTimer, &QTimer::timeout,
        this, &SignalingClient::onReconnectTimer);

    QObject::connect(m_ws, &QWebSocket::connected,
        this, &SignalingClient::onWsConnected);
    QObject::connect(m_ws, &QWebSocket::disconnected,
        this, &SignalingClient::onWsDisconnected);
    QObject::connect(m_ws, &QWebSocket::textMessageReceived,
        this, &SignalingClient::onWsTextMessageReceived);
    QObject::connect(m_ws,
        QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::error),
        this, &SignalingClient::onWsError);
}

SignalingClient::~SignalingClient()
{
    m_pingTimer->stop();
    m_reconnectTimer->stop();
    if (m_ws->state() != QAbstractSocket::UnconnectedState) {
        m_ws->abort();
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void SignalingClient::connectToServer(const QString& url)
{
    if (m_state == State::Connected || m_state == State::Upgrading ||
        m_state == State::Polling)
    {
        return;
    }

    resetReconnectState();

    QString normalised = url;
    if (normalised.startsWith(QLatin1String("wss://"))) {
        normalised.replace(0, 6, QLatin1String("https://"));
    }
    else if (normalised.startsWith(QLatin1String("ws://"))) {
        normalised.replace(0, 5, QLatin1String("http://"));
    }
    m_baseUrl = QUrl(normalised);

    // Skip HTTP polling — connect directly via WebSocket (EIO v4 pure-WS mode).
    // Server will send 0{...} over WS; we reply with 40 then wait for server 40.
    m_state = State::Upgrading;
    m_sid.clear();
    m_ws->open(buildWsUrl());
}

void SignalingClient::on(const QString& event, EventCallback cb)
{
    m_eventHandlers.insert(event, std::move(cb));
}

void SignalingClient::disconnect()
{
    m_reconnectAttempts = k_maxReconnectAttempts;
    m_pingTimer->stop();
    m_reconnectTimer->stop();

    if (m_state == State::Connected) {
        sendRaw(QString(EIO_MESSAGE) + QString(SIO_DISCONNECT));
        sendRaw(QString(EIO_CLOSE));
    }

    m_ws->close();
    m_state = State::Idle;
    m_sid.clear();
}

// ---------------------------------------------------------------------------
// WebSocket slots
// ---------------------------------------------------------------------------

void SignalingClient::onWsConnected()
{
    // Pure-WS mode: wait for the server's EIO OPEN (0{...}) packet.
    // We send SIO CONNECT (40) only after parsing it in handleEngineIoFrame.
    emit signalingLog(QStringLiteral("[WS] WebSocket connected — waiting for EIO OPEN"));
}

void SignalingClient::onWsDisconnected()
{
    const bool wasConnected = (m_state == State::Connected);
    m_pingTimer->stop();
    m_state = State::Idle;

    emit signalingLog(QStringLiteral("[WS] WebSocket disconnected"));

    if (wasConnected) {
        emit disconnected();
    }

    if (m_reconnectAttempts < k_maxReconnectAttempts) {
        scheduleReconnect();
    }
}

void SignalingClient::onWsTextMessageReceived(const QString& message)
{
    emit signalingLog(QStringLiteral("[WS] RX: %1").arg(
        message.length() > 120 ? message.left(120) + QStringLiteral("…") : message));
    handleEngineIoFrame(message);
}

void SignalingClient::onWsError(QAbstractSocket::SocketError /*error*/)
{
    const QString msg = m_ws->errorString();
    emit signalingLog(QStringLiteral("[WS] Error: %1").arg(msg));
    emit authError(msg);
}

// ---------------------------------------------------------------------------
// Timer slots
// ---------------------------------------------------------------------------

void SignalingClient::onPingTimer()
{
    // EIO v4: server owns the ping/pong cycle.
    // If this timer fires it means the server has gone silent — treat as timeout.
    emit signalingLog(QStringLiteral("[EIO] Ping timeout — closing"));
    m_ws->close();
}

void SignalingClient::onReconnectTimer()
{
    if (m_reconnectAttempts >= k_maxReconnectAttempts) { return; }
    emit signalingLog(QStringLiteral("[SIG] Reconnect attempt %1").arg(m_reconnectAttempts));
    connectToServer(m_baseUrl.toString());
}

// ---------------------------------------------------------------------------
// Engine.IO / Socket.IO framing
// ---------------------------------------------------------------------------

void SignalingClient::sendRaw(const QString& frame)
{
    if (m_ws->state() == QAbstractSocket::ConnectedState) {
        emit signalingLog(QStringLiteral("[WS] TX: %1").arg(
            frame.length() > 120 ? frame.left(120) + QStringLiteral("…") : frame));
        m_ws->sendTextMessage(frame);
    }
}

void SignalingClient::handleEngineIoFrame(const QString& frame)
{
    if (frame.isEmpty()) { return; }

    const QChar type = frame[0];

    // -----------------------------------------------------------------------
    // EIO OPEN (0) — can arrive over WS in some non-ASGI configurations.
    // -----------------------------------------------------------------------
    if (type == EIO_OPEN) {
        const QString jsonPart = frame.mid(1);
        if (!jsonPart.isEmpty()) {
            try {
                const nlohmann::json obj =
                    nlohmann::json::parse(jsonPart.toStdString());
                if (obj.contains("sid") && obj["sid"].is_string())
                    m_sid = QString::fromStdString(obj["sid"].get<std::string>());
                m_pingInterval = obj.value("pingInterval", m_pingInterval);
                m_pingTimeout = obj.value("pingTimeout", m_pingTimeout);
            }
            catch (...) {}
        }
        // Server sent EIO OPEN over WS — now send SIO CONNECT (exactly once).
        emit signalingLog(QStringLiteral("[EIO] OPEN received — sending SIO CONNECT"));
        sendRaw(QString(EIO_MESSAGE) + QString(SIO_CONNECT));
        return;
    }

    if (type == EIO_CLOSE) {
        m_ws->close();
        return;
    }

    // -----------------------------------------------------------------------
    // EIO PING (2) — server-initiated.
    // Payload "probe" = classic upgrade handshake.
    // Empty payload   = heartbeat ping (send pong back).
    // -----------------------------------------------------------------------
    if (type == EIO_PING) {
        const QString payload = frame.mid(1);
        sendRaw(QString(EIO_PONG) + payload);   // respond to server ping

        // Reset the timeout watchdog: the next ping won't arrive for
        // pingInterval ms, then we allow pingTimeout ms grace. Firing before
        // the next ping is due would cause a spurious disconnect.
        m_pingTimer->start(m_pingInterval + m_pingTimeout);
        return;
    }

    if (type == EIO_PONG) { return; }

    // -----------------------------------------------------------------------
    // EIO MESSAGE (4) — carries a Socket.IO packet.
    // -----------------------------------------------------------------------
    if (type == EIO_MESSAGE) {
        handleSocketIoPacket(frame.mid(1));
        return;
    }

    // EIO_UPGRADE (5) and anything else — ignored.
}

void SignalingClient::handleSocketIoPacket(const QString& payload)
{
    if (payload.isEmpty()) { return; }

    const QChar sioType = payload[0];

    // -----------------------------------------------------------------------
    // SIO CONNECT (0) — namespace accepted by server.
    // Arrives as "40" or "40{...}" in python-socketio.
    // -----------------------------------------------------------------------
    if (sioType == SIO_CONNECT) {
        if (m_state != State::Connected) {
            m_state = State::Connected;
            resetReconnectState();
            m_pingTimer->setSingleShot(true);
            // Use pingInterval + pingTimeout so the watchdog survives the full
            // server ping cycle before declaring the connection dead.
            m_pingTimer->start(m_pingInterval + m_pingTimeout);
            emit signalingLog(QStringLiteral("[SIO] Namespace connected ✓"));
            emit connected();
        }
        return;
    }

    // -----------------------------------------------------------------------
    // SIO DISCONNECT (1)
    // -----------------------------------------------------------------------
    if (sioType == SIO_DISCONNECT) {
        m_pingTimer->stop();
        m_state = State::Idle;
        m_ws->close();
        emit signalingLog(QStringLiteral("[SIO] Server disconnected namespace"));
        emit disconnected();
        return;
    }

    // -----------------------------------------------------------------------
    // SIO EVENT (2) — "2[\"eventName\", ...args]"  or  "2<ackId>[...]"
    // -----------------------------------------------------------------------
    if (sioType == SIO_EVENT) {
        QString rest = payload.mid(1);

        int ackId = -1;
        int jsonStart = 0;
        while (jsonStart < rest.size() && rest[jsonStart].isDigit()) { ++jsonStart; }
        if (jsonStart > 0) {
            ackId = rest.left(jsonStart).toInt();
            rest = rest.mid(jsonStart);
        }

        try {
            nlohmann::json arr = nlohmann::json::parse(rest.toStdString());
            if (!arr.is_array() || arr.empty()) { return; }

            const QString eventName =
                QString::fromStdString(arr[0].get<std::string>());

            emit signalingLog(
                QStringLiteral("[SIO] event: %1").arg(eventName));

            nlohmann::json args = nlohmann::json::array();
            for (std::size_t i = 1; i < arr.size(); ++i) { args.push_back(arr[i]); }

            auto it = m_eventHandlers.find(eventName);
            if (it != m_eventHandlers.end() && it.value()) { it.value()(args); }

            if (ackId >= 0) {
                sendRaw(QString(EIO_MESSAGE) + QString(SIO_ACK)
                    + QString::number(ackId) + QStringLiteral("[]"));
            }
        }
        catch (const nlohmann::json::exception&) {}
        return;
    }

    // -----------------------------------------------------------------------
    // SIO ACK (3) — "3<ackId>[...args]"
    // -----------------------------------------------------------------------
    if (sioType == SIO_ACK) {
        QString rest = payload.mid(1);
        int     jsonStart = 0;
        while (jsonStart < rest.size() && rest[jsonStart].isDigit()) { ++jsonStart; }
        if (jsonStart == 0) { return; }

        const int ackId = rest.left(jsonStart).toInt();
        rest = rest.mid(jsonStart);

        auto it = m_ackCallbacks.find(ackId);
        if (it == m_ackCallbacks.end()) { return; }

        AckCallback cb = std::move(it.value());
        m_ackCallbacks.erase(it);

        try {
            const nlohmann::json args = nlohmann::json::parse(rest.toStdString());
            if (cb) { cb(args); }
        }
        catch (...) {
            if (cb) { cb(nlohmann::json::array()); }
        }
        return;
    }

    // -----------------------------------------------------------------------
    // SIO CONNECT_ERROR (4)
    // -----------------------------------------------------------------------
    if (sioType == SIO_CONNECT_ERROR) {
        QString reason = QStringLiteral("Connection error");
        const QString jsonPart = payload.mid(1);
        if (!jsonPart.isEmpty()) {
            try {
                const nlohmann::json obj =
                    nlohmann::json::parse(jsonPart.toStdString());
                if (obj.contains("message") && obj["message"].is_string()) {
                    reason = QString::fromStdString(
                        obj["message"].get<std::string>());
                }
            }
            catch (...) {}
        }
        emit signalingLog(QStringLiteral("[SIO] CONNECT_ERROR: %1").arg(reason));
        emit authError(reason);
        return;
    }
}

QString SignalingClient::buildEventFrame(const QString& event,
    const nlohmann::json& data,
    int ackId) const
{
    nlohmann::json arr = nlohmann::json::array();
    arr.push_back(event.toStdString());
    arr.push_back(data);

    QString frame = QString(EIO_MESSAGE) + QString(SIO_EVENT);
    if (ackId >= 0) { frame += QString::number(ackId); }
    frame += QString::fromStdString(arr.dump());
    return frame;
}

void SignalingClient::scheduleReconnect()
{
    if (m_reconnectAttempts >= k_maxReconnectAttempts) { return; }
    ++m_reconnectAttempts;
    m_state = State::Reconnecting;
    const int delayMs = k_reconnectBaseMs
        * static_cast<int>(std::pow(2.0, m_reconnectAttempts - 1));
    m_reconnectTimer->start(delayMs);
    emit signalingLog(
        QStringLiteral("[SIG] Reconnecting in %1 ms (attempt %2)")
        .arg(delayMs).arg(m_reconnectAttempts));
}

void SignalingClient::resetReconnectState()
{
    m_reconnectAttempts = 0;
    m_reconnectTimer->stop();
}

QUrl SignalingClient::buildWsUrl() const
{
    QUrl wsUrl = m_baseUrl;
    const QString scheme = wsUrl.scheme().toLower();
    wsUrl.setScheme(scheme == QLatin1String("https")
        ? QStringLiteral("wss") : QStringLiteral("ws"));
    wsUrl.setPath(QString::fromLatin1(k_socketIoPath));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("EIO"), QString::fromLatin1(k_eioVersion));
    q.addQueryItem(QStringLiteral("transport"), QStringLiteral("websocket"));
    if (!m_sid.isEmpty()) { q.addQueryItem(QStringLiteral("sid"), m_sid); }
    wsUrl.setQuery(q);
    return wsUrl;
}

void SignalingClient::emitEvent(const QString& event,
    const nlohmann::json& data,
    AckCallback cb)
{
    if (m_state != State::Connected) { return; }

    int ackId = -1;
    if (cb) {
        ackId = m_nextAckId.fetch_add(1, std::memory_order_relaxed);
        m_ackCallbacks.insert(ackId, std::move(cb));
    }
    sendRaw(buildEventFrame(event, data, ackId));
}
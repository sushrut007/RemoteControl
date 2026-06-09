#pragma once

#include <QObject>
#include <QQueue>
#include <QMutex>
#include <QHash>
#include <QElapsedTimer>
#include <QString>
#include <QAtomicInt>
#include "AppState.h"
#include <nlohmann/json.hpp>

class InputInjector;
QT_FORWARD_DECLARE_CLASS(QThread)
QT_FORWARD_DECLARE_CLASS(QTimer)

// ---------------------------------------------------------------------------
// ControlEventHandler
//
// Sits between SignalingClient (socket thread) and InputInjector (Win32
// SendInput), providing:
//
//  - Validation: sender must be in the active room and deviceControl must be
//    enabled (setEnabled / setActiveRoom).
//  - Rate limiting: ≤ 100 mouse events/s and ≤ 30 keyboard events/s per
//    sender.  Excess events are silently dropped and rateLimitExceeded is
//    emitted.
//  - Async dispatch: events are pushed onto a QQueue and drained on a
//    dedicated QThread at 60 Hz so the socket event thread is never stalled.
//
// Thread safety
// -------------
//  handleControl / handleKeyboard / handleMouse may be called from any thread.
//  The queue is protected by m_queueMutex.  The drain timer and InputInjector
//  run exclusively on m_workerThread.
// ---------------------------------------------------------------------------
class ControlEventHandler : public QObject
{
    Q_OBJECT

public:
    explicit ControlEventHandler(InputInjector* injector,
        QObject* parent = nullptr);
    ~ControlEventHandler() override;

    ControlEventHandler(const ControlEventHandler&) = delete;
    ControlEventHandler& operator=(const ControlEventHandler&) = delete;

    // -----------------------------------------------------------------------
    // Configuration
    // -----------------------------------------------------------------------

    /// Enable or disable all event processing.
    void setEnabled(bool enabled);

    /// Register the active room id and whether device control is permitted.
    void setActiveRoom(const QString& roomId, bool deviceControlEnabled);

    /// Register a sender as a valid peer in the current room.
    void addPeer(const QString& peerId);

    /// Remove a sender (e.g. peer left).
    void removePeer(const QString& peerId);

    // -----------------------------------------------------------------------
    // Event entry points – safe to call from any thread
    // -----------------------------------------------------------------------

    /// Generic control command (e.g. clipboard, file-transfer triggers).
    void handleControl(const nlohmann::json& data);

    /// Keyboard event from the signaling layer.
    void handleKeyboard(const nlohmann::json& data);

    /// Mouse event from the signaling layer.
    void handleMouse(const nlohmann::json& data);

signals:
    // -----------------------------------------------------------------------
    // Qt signals – emitted on the worker thread; use queued connections if
    // consuming on the main thread.
    // -----------------------------------------------------------------------

    /// Emitted when a valid control command is dispatched.
    void controlReceived(const QString& senderId, const QString& command);

    /// Emitted when a sender exceeds the per-type rate limit.
    void rateLimitExceeded(const QString& senderId);

private slots:
    /// Drain up to one tick's worth of events from the queue (called at 60 Hz).
    void drainQueue();

private:
    // -----------------------------------------------------------------------
    // Rate-limiting bookkeeping per sender × event type
    // -----------------------------------------------------------------------

    struct RateCounter {
        int          count{ 0 };
        QElapsedTimer window;
    };

    struct SenderState {
        RateCounter mouse;
        RateCounter keyboard;
    };

    /// Returns true if the event is within the allowed rate; updates counter.
    bool checkRate(RateCounter& counter, int maxPerSec, const QString& senderId);

    // -----------------------------------------------------------------------
    // Validation helpers
    // -----------------------------------------------------------------------

    /// Returns true if data["senderId"] is a valid, authorised peer.
    bool validate(const nlohmann::json& data, QString& outSenderId) const;

    // -----------------------------------------------------------------------
    // Dispatch (called on worker thread)
    // -----------------------------------------------------------------------

    void dispatchKeyboard(const ControlEvent& ev);
    void dispatchMouse(const ControlEvent& ev);
    void dispatchControl(const ControlEvent& ev);

    // -----------------------------------------------------------------------
    // Members
    // -----------------------------------------------------------------------

    InputInjector* m_injector{ nullptr }; ///< lives on m_workerThread

    QThread* m_workerThread{ nullptr };
    QTimer* m_drainTimer{ nullptr };     ///< 60 Hz, lives on m_workerThread

    // Event queue
    QQueue<ControlEvent> m_queue;
    mutable QMutex       m_queueMutex;

    // Configuration (guarded by m_configMutex)
    mutable QMutex m_configMutex;
    bool           m_enabled{ false };
    bool           m_deviceControlEnabled{ false };
    QString        m_activeRoomId;
    QHash<QString, bool> m_peers; ///< peerId → true (authorised)

    // Rate-limit state per sender (accessed only on worker thread)
    QHash<QString, SenderState> m_senderState;

    // Rate limits (events per second)
    static constexpr int k_maxMouseHz = 120;  // raised from 100 — smooths high-DPI moves
    static constexpr int k_maxKeyboardHz = 120;  // raised from 30 — prevents keystroke drops

    // Drain tick interval
    static constexpr int k_drainIntervalMs = 1000 / 60; // ~16 ms
};
#pragma once

#include <QObject>
#include <QMap>
#include <QTimer>
#include <QString>
#include <QUrl>
#include <QtNetwork/QAbstractSocket>
#include <functional>
#include <atomic>

#include <nlohmann/json.hpp>

QT_FORWARD_DECLARE_CLASS(QWebSocket)
QT_FORWARD_DECLARE_CLASS(QNetworkAccessManager)
QT_FORWARD_DECLARE_CLASS(QNetworkReply)

// ---------------------------------------------------------------------------
// SignalingClient
//
// Minimal Socket.IO v4 client over QWebSocket.
//
// Protocol layers
// ---------------
//  Engine.IO packet types (single leading digit in text frames):
//    0 = open   (server → client, JSON handshake payload)
//    1 = close
//    2 = ping   (server → client)
//    3 = pong   (client → server)
//    4 = message (carries a Socket.IO packet as its payload)
//
//  Socket.IO packet types (digit immediately following Engine.IO '4'):
//    0 = connect
//    1 = disconnect
//    2 = event      ("4 2 [\"eventName\", ...data]")
//    3 = ack        ("4 3 [ackId, ...data]")
//    4 = connect_error
//
// Connection sequence
// -------------------
//  1. HTTP GET /socket.io/?EIO=4&transport=polling  → receive open packet,
//     extract sid / pingInterval / pingTimeout.
//  2. Upgrade to WebSocket: ws://host/socket.io/?EIO=4&transport=websocket&sid=…
//  3. Send Engine.IO probe ping ("2probe"), expect "3probe" pong, then send "5".
//  4. Server sends Socket.IO connect packet ("40"), client emits connected().
// ---------------------------------------------------------------------------
class SignalingClient : public QObject
{
    Q_OBJECT

public:
    // -----------------------------------------------------------------------
    // Callback types
    // -----------------------------------------------------------------------

    /// Called when the server acknowledges an emitted event.
    /// Receives the JSON array of acknowledgement arguments (may be empty).
    using AckCallback = std::function<void(const nlohmann::json& ackArgs)>;

    /// Called when a server-emitted event arrives.
    /// Receives the JSON array of event arguments (excludes the event name).
    using EventCallback = std::function<void(const nlohmann::json& args)>;

    // -----------------------------------------------------------------------
    // Lifecycle
    // -----------------------------------------------------------------------

    explicit SignalingClient(QObject* parent = nullptr);
    ~SignalingClient() override;

    SignalingClient(const SignalingClient&) = delete;
    SignalingClient& operator=(const SignalingClient&) = delete;

    // -----------------------------------------------------------------------
    // Public API
    // -----------------------------------------------------------------------

    /// Begin connecting to a Socket.IO server.
    /// @param url  Base server URL, e.g. "https://example.com" or
    ///             "wss://example.com".  The Socket.IO path and EIO query
    ///             parameters are appended automatically.
    void connectToServer(const QString& url);

    /// Emit a Socket.IO event to the server.
    /// @param event  Event name.
    /// @param data   JSON value (object, array, or primitive) sent as the
    ///               second element of the Socket.IO event array.
    /// @param cb     Optional acknowledgement callback; if provided the packet
    ///               is sent with an ack-id and the callback is invoked when
    ///               the server sends the matching ack packet.

    void emitEvent(const QString& event, const nlohmann::json& data,
        AckCallback cb = nullptr);

    /// Register a handler for a server-emitted event.
    /// Calling on() twice with the same event name replaces the previous handler.
    void on(const QString& event, EventCallback cb);

    /// Gracefully disconnect from the server.
    void disconnect();

    // -----------------------------------------------------------------------
    // Accessors
    // -----------------------------------------------------------------------
#ifdef connect
#  undef connect   // guard against POSIX/Winsock macro clash
#endif
    bool isConnected() const { return m_state == State::Connected; }

signals:
    // -----------------------------------------------------------------------
    // Qt signals – always emitted on the Qt main thread
    // -----------------------------------------------------------------------

    /// Emitted once the Socket.IO handshake is complete and the namespace
    /// connect packet has been received.
    void connected();

    /// Emitted when the WebSocket connection closes (cleanly or otherwise).
    void disconnected();

    /// Emitted when the server sends a connect_error packet (e.g. JWT rejected).
    /// @param reason  Error description from the server payload.
    void authError(const QString& reason);
    void signalingLog(const QString& message);

private slots:
    // WebSocket events
    void onWsConnected();
    void onWsDisconnected();
    void onWsTextMessageReceived(const QString& message);
    void onWsError(QAbstractSocket::SocketError error);

    // Timers
    void onPingTimer();
    void onReconnectTimer();

private:
    // -----------------------------------------------------------------------
    // Internal state machine
    // -----------------------------------------------------------------------

    enum class State {
        Idle,
        Polling,       // HTTP long-poll handshake in progress
        Upgrading,     // WebSocket upgrade / probe handshake
        Connected,     // Socket.IO namespace connected
        Reconnecting,  // Waiting before the next attempt
    };

    // -----------------------------------------------------------------------
    // Engine.IO / Socket.IO framing
    // -----------------------------------------------------------------------

    /// Send a raw Engine.IO text frame.
    void sendRaw(const QString& frame);

    /// Parse and dispatch an incoming Engine.IO text frame.
    void handleEngineIoFrame(const QString& frame);

    /// Parse and dispatch a Socket.IO packet (the payload after "4").
    void handleSocketIoPacket(const QString& payload);

    /// Build a Socket.IO event frame string.
    /// If ackId >= 0 the id is embedded between the type digit and the array.
    QString buildEventFrame(const QString& event,
        const nlohmann::json& data,
        int ackId = -1) const;

    // -----------------------------------------------------------------------
    // Reconnection
    // -----------------------------------------------------------------------

    void scheduleReconnect();
    void resetReconnectState();

    // -----------------------------------------------------------------------
    // Helpers
    // -----------------------------------------------------------------------

    /// Build the WebSocket URL from the stored base URL and sid.
    QUrl buildWsUrl() const;

    // -----------------------------------------------------------------------
    // Members
    // -----------------------------------------------------------------------

    // Connection state
    State   m_state{ State::Idle };
    QUrl    m_baseUrl;          ///< Normalised base URL (http/https scheme)
    QString m_sid;              ///< Engine.IO session id from open packet
    int     m_pingInterval{ 25000 };  ///< ms, from server open packet
    int     m_pingTimeout{ 20000 };   ///< ms, from server open packet

    // Socket.IO namespace (always "/" for default namespace)
    static constexpr const char* k_namespace = "";

    // Networking
    QWebSocket* m_ws{ nullptr };
    QNetworkAccessManager* m_nam{ nullptr };

    // Heartbeat
    QTimer* m_pingTimer{ nullptr };

    // Reconnection
    QTimer* m_reconnectTimer{ nullptr };
    int     m_reconnectAttempts{ 0 };
    static constexpr int k_maxReconnectAttempts = 5;
    static constexpr int k_reconnectBaseMs = 1000; ///< Initial back-off

    // Ack tracking
    std::atomic<int>        m_nextAckId{ 0 };
    QMap<int, AckCallback>  m_ackCallbacks;

    // Event handlers
    QMap<QString, EventCallback> m_eventHandlers;
};
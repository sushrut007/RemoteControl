#include "RoomManager.h"

#include "../network/SignalingClient.h"
#include "../mediasoup/MediasoupClient.h"

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

RoomManager::RoomManager(SignalingClient* signalingClient,
    MediasoupClient* mediasoupClient,
    QObject* parent)
    : QObject(parent)
    , m_signaling(signalingClient)
    , m_mediasoup(mediasoupClient)
{
    Q_ASSERT(m_signaling);
    Q_ASSERT(m_mediasoup);

    // Forward mediasoup-level errors as connectionFailed signals.
    QObject::connect(m_mediasoup, &MediasoupClient::mediaError,
        this, [this](const QString& msg) {
            emit connectionFailed(msg);
        });
}

RoomManager::~RoomManager() = default;

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void RoomManager::joinRoom(const QString& roomId,
    const QString& displayName,
    const nlohmann::json& metadata)
{
    m_roomId = roomId;
    m_displayName = displayName;
    m_streamReadyEmitted = false;
    m_peers.clear();

    // -----------------------------------------------------------------------
    // Register persistent server-push event handlers.
    // These remain active for the lifetime of the room session.
    // -----------------------------------------------------------------------
    m_signaling->on(QStringLiteral("new-producer"),
        [this](const nlohmann::json& args) { onNewProducer(args); });

    m_signaling->on(QStringLiteral("stream-ready"),
        [this](const nlohmann::json& args) { onStreamReady(args); });

    m_signaling->on(QStringLiteral("peer-joined"),
        [this](const nlohmann::json& args) { onPeerJoined(args); });

    m_signaling->on(QStringLiteral("peer-left"),
        [this](const nlohmann::json& args) { onPeerLeft(args); });

    // -----------------------------------------------------------------------
    // Step 1: emit "join-room" and wait for the server ack.
    // -----------------------------------------------------------------------
    nlohmann::json joinPayload = {
        { "roomId",      roomId.toStdString()      },
        { "displayName", displayName.toStdString()  },
        { "metadata",    metadata                   }
    };

    m_signaling->emitEvent(
        QStringLiteral("join-room"),
        joinPayload,
        [this](const nlohmann::json& ackArgs) {
            step2_loadDevice(ackArgs);
        });
}

void RoomManager::leaveRoom()
{
    m_signaling->emitEvent(QStringLiteral("leave-room"),
        nlohmann::json::object());
    m_peers.clear();
    m_localPeerId.clear();
    m_roomId.clear();
    m_rtpCapabilities = {};
    m_sendTransportParams = {};
    m_recvTransportParams = {};
    m_sendTransportId.clear();
    m_recvTransportId.clear();
    m_streamReadyEmitted = false;
}

// ---------------------------------------------------------------------------
// Step 2 – load device
// ---------------------------------------------------------------------------

void RoomManager::step2_loadDevice(const nlohmann::json& ackArgs)
{
    // Expected ack shape:
    //   { "rtpCapabilities": {...}, "peers": [...], "peerId": "..." }
    // ackArgs is a JSON array where element 0 is the response object.
    const nlohmann::json& response = ackArgs.is_array() && !ackArgs.empty()
        ? ackArgs[0]
        : ackArgs;

    if (response.contains("error")) {
        failWith(QString::fromStdString(
            response["error"].value("message", "join-room rejected")));
        return;
    }

    if (!response.contains("rtpCapabilities")) {
        failWith(QStringLiteral("join-room ack missing rtpCapabilities"));
        return;
    }

    // Store local peer id
    if (response.contains("peerId") && response["peerId"].is_string()) {
        m_localPeerId =
            QString::fromStdString(response["peerId"].get<std::string>());
    }

    // Populate initial peers map
    if (response.contains("peers") && response["peers"].is_array()) {
        for (const auto& p : response["peers"]) {
            PeerInfo info = peerInfoFromJson(p);
            m_peers.insert(info.id, info);
        }
    }

    m_rtpCapabilities = response["rtpCapabilities"];
    m_mediasoup->loadDevice(m_rtpCapabilities);

    step3_createSendTransport();
}

// ---------------------------------------------------------------------------
// Step 3 – create send transport
// ---------------------------------------------------------------------------

void RoomManager::step3_createSendTransport()
{
    nlohmann::json payload = { { "direction", "send" } };

    m_signaling->emitEvent(
        QStringLiteral("create-transport"),
        payload,
        [this](const nlohmann::json& ackArgs) {
            step4_onSendTransportConnected(ackArgs);
        });
}

// ---------------------------------------------------------------------------
// Step 4 – apply send transport params, wire up connect callback
// ---------------------------------------------------------------------------

void RoomManager::step4_onSendTransportConnected(const nlohmann::json& ackArgs)
{
    const nlohmann::json& transportParams =
        ackArgs.is_array() && !ackArgs.empty() ? ackArgs[0] : ackArgs;

    if (transportParams.contains("error")) {
        failWith(QString::fromStdString(
            transportParams["error"].value("message", "create-transport (send) failed")));
        return;
    }

    if (!transportParams.contains("id")) {
        failWith(QStringLiteral("create-transport (send) ack missing 'id'"));
        return;
    }

    m_sendTransportParams = transportParams;
    m_sendTransportId =
        QString::fromStdString(transportParams["id"].get<std::string>());

    // Create the local send transport.
    m_mediasoup->createSendTransport(transportParams);

    // Wire the transportConnected signal → tell the server our DTLS parameters.
    QObject::connect(
        m_mediasoup, &MediasoupClient::transportConnected,
        this,
        [this](const QString& transportId, const QString& direction) {
            if (direction != QLatin1String("send") ||
                transportId != m_sendTransportId)
            {
                return;
            }

            nlohmann::json connectPayload = {
                { "transportId",    transportId.toStdString()                        },
                { "dtlsParameters", m_sendTransportParams.value("dtlsParameters",
                                        nlohmann::json::object())                    }
            };

            m_signaling->emitEvent(QStringLiteral("connect-transport"), connectPayload);
        },
        Qt::SingleShotConnection);

    // Wire the producerCreated signal → tell the server about the producer.
    QObject::connect(
        m_mediasoup, &MediasoupClient::producerCreated,
        this,
        [this](const QString& producerId, const QString& kind) {
            nlohmann::json producePayload = {
                { "transportId", m_sendTransportId.toStdString() },
                { "kind",        kind.toStdString()              },
                { "producerId",  producerId.toStdString()        }
            };
            m_signaling->emitEvent(QStringLiteral("produce"), producePayload);
        });

    step6_createRecvTransport();
}

// ---------------------------------------------------------------------------
// Step 6 – create recv transport
// ---------------------------------------------------------------------------

void RoomManager::step6_createRecvTransport()
{
    nlohmann::json payload = { { "direction", "recv" } };

    m_signaling->emitEvent(
        QStringLiteral("create-transport"),
        payload,
        [this](const nlohmann::json& ackArgs) {
            step7_onRecvTransportConnected(ackArgs);
        });
}

// ---------------------------------------------------------------------------
// Step 7 – apply recv transport params
// ---------------------------------------------------------------------------

void RoomManager::step7_onRecvTransportConnected(const nlohmann::json& ackArgs)
{
    const nlohmann::json& transportParams =
        ackArgs.is_array() && !ackArgs.empty() ? ackArgs[0] : ackArgs;

    if (transportParams.contains("error")) {
        failWith(QString::fromStdString(
            transportParams["error"].value("message", "create-transport (recv) failed")));
        return;
    }

    if (!transportParams.contains("id")) {
        failWith(QStringLiteral("create-transport (recv) ack missing 'id'"));
        return;
    }

    m_recvTransportParams = transportParams;
    m_recvTransportId =
        QString::fromStdString(transportParams["id"].get<std::string>());

    m_mediasoup->createRecvTransport(transportParams);

    // Wire recv transport connect → tell the server our DTLS parameters.
    QObject::connect(
        m_mediasoup, &MediasoupClient::transportConnected,
        this,
        [this](const QString& transportId, const QString& direction) {
            if (direction != QLatin1String("recv") ||
                transportId != m_recvTransportId)
            {
                return;
            }

            nlohmann::json connectPayload = {
                { "transportId",    transportId.toStdString()                        },
                { "dtlsParameters", m_recvTransportParams.value("dtlsParameters",
                                        nlohmann::json::object())                    }
            };

            m_signaling->emitEvent(QStringLiteral("connect-transport"), connectPayload);
        },
        Qt::SingleShotConnection);

    // Both transports ready – emit roomJoined.
    RoomInfo info;
    info.roomId = m_roomId;
    info.localPeerId = m_localPeerId;
    for (const auto& p : m_peers) {
        info.peers.append(p);
    }
    Q_EMIT roomJoined(info);;
}

// ---------------------------------------------------------------------------
// Step 8 – "new-producer" server event
// ---------------------------------------------------------------------------

void RoomManager::onNewProducer(const nlohmann::json& args)
{
    // Expected: [{ producerId, peerId, kind }]
    const nlohmann::json& data =
        args.is_array() && !args.empty() ? args[0] : args;

    if (!data.contains("producerId")) {
        return;
    }

    const std::string producerId =
        data["producerId"].get<std::string>();
    const std::string kind =
        data.value("kind", "video");

    nlohmann::json consumePayload = {
        { "transportId",      m_recvTransportId.toStdString() },
        { "producerId",       producerId          },
        { "rtpCapabilities",  m_rtpCapabilities   }
    };

    m_signaling->emitEvent(
        QStringLiteral("consume"),
        consumePayload,
        [this, producerId, kind](const nlohmann::json& ackArgs) {
            const nlohmann::json& consumerParams =
                ackArgs.is_array() && !ackArgs.empty() ? ackArgs[0] : ackArgs;

            if (consumerParams.contains("error")) {
                // Non-fatal: a single consumer failing should not break the room.
                return;
            }

            m_mediasoup->consume(
                consumerParams,
                [this](const std::string& consumerId,
                    const nlohmann::json& /*consumerInfo*/) {
                        // Tell the server to start sending media.
                        nlohmann::json resumePayload = {
                            { "consumerId", consumerId }
                        };
                        m_signaling->emitEvent(
                            QStringLiteral("resume-consumer"), resumePayload);
                });
        });
}

// ---------------------------------------------------------------------------
// Step 9 – "stream-ready" server event
// ---------------------------------------------------------------------------

void RoomManager::onStreamReady(const nlohmann::json& /*args*/)
{
    if (!m_streamReadyEmitted) {
        m_streamReadyEmitted = true;
        emit streamReady();
    }
}

// ---------------------------------------------------------------------------
// Peer lifecycle events
// ---------------------------------------------------------------------------

void RoomManager::onPeerJoined(const nlohmann::json& args)
{
    const nlohmann::json& data =
        args.is_array() && !args.empty() ? args[0] : args;

    PeerInfo info = peerInfoFromJson(data);
    if (info.id.isEmpty()) {
        return;
    }

    m_peers.insert(info.id, info);
    Q_EMIT peerJoined(info);
}

void RoomManager::onPeerLeft(const nlohmann::json& args)
{
    const nlohmann::json& data =
        args.is_array() && !args.empty() ? args[0] : args;

    QString peerId;
    if (data.contains("peerId") && data["peerId"].is_string()) {
        peerId = QString::fromStdString(data["peerId"].get<std::string>());
    }
    if (peerId.isEmpty()) { return; }

    m_peers.remove(peerId);
    Q_EMIT peerLeft(peerId);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

void RoomManager::failWith(const QString& reason)
{
    emit connectionFailed(reason);
}

PeerInfo RoomManager::peerInfoFromJson(const nlohmann::json& obj)
{
    PeerInfo info;
    if (obj.contains("peerId") && obj["peerId"].is_string()) {
        info.id = QString::fromStdString(obj["peerId"].get<std::string>());
    }
    if (obj.contains("displayName") && obj["displayName"].is_string()) {
        info.displayName =
            QString::fromStdString(obj["displayName"].get<std::string>());
    }
    info.metadata = obj;
    return info;
}
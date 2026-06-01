#pragma once

#include <QObject>
#include <QMap>
#include <QString>
#include "AppState.h"

#include <nlohmann/json.hpp>

class SignalingClient;
class MediasoupClient;


// ---------------------------------------------------------------------------
// RoomManager
//
// Orchestrates the full mediasoup join sequence:
//
//  1  signalingClient.emit("join-room")
//        → ack: { rtpCapabilities, peers, peerId }
//  2  mediasoupClient.loadDevice(rtpCapabilities)
//  3  signalingClient.emit("create-transport", {direction:"send"})
//        → ack: transportParams  →  mediasoupClient.createSendTransport()
//  4  on send-transport connect:
//        signalingClient.emit("connect-transport", {transportId, dtlsParameters})
//  5  on produce:
//        signalingClient.emit("produce", {transportId, kind, rtpParameters})
//        → ack: { producerId }
//  6  signalingClient.emit("create-transport", {direction:"recv"})
//        → ack: transportParams  →  mediasoupClient.createRecvTransport()
//  7  on recv-transport connect:
//        signalingClient.emit("connect-transport", {transportId, dtlsParameters})
//  8  on server event "new-producer":
//        signalingClient.emit("consume", {producerId, rtpCapabilities})
//        → ack: consumerParams  →  mediasoupClient.consume()
//        signalingClient.emit("resume-consumer", {consumerId})
//  9  on server event "stream-ready" (or when first consumer is ready):
//        emit streamReady()
// ---------------------------------------------------------------------------
class RoomManager : public QObject
{
    Q_OBJECT

public:
    explicit RoomManager(SignalingClient* signalingClient,
        MediasoupClient* mediasoupClient,
        QObject* parent = nullptr);
    ~RoomManager() override;

    RoomManager(const RoomManager&) = delete;
    RoomManager& operator=(const RoomManager&) = delete;

    // -----------------------------------------------------------------------
    // Public API
    // -----------------------------------------------------------------------

    /// Begin the join sequence.
    /// @param roomId       Room identifier to join.
    /// @param displayName  Local participant display name.
    /// @param metadata     Optional JSON metadata to include in "join-room".
    void joinRoom(const QString& roomId,
        const QString& displayName,
        const nlohmann::json& metadata = nlohmann::json::object());

    /// Leave the room and clean up all transports/consumers.
    void leaveRoom();

    // -----------------------------------------------------------------------
    // Accessors
    // -----------------------------------------------------------------------

    QString             localPeerId() const { return m_localPeerId; }
    QString             roomId()      const { return m_roomId; }
    QMap<QString, PeerInfo> peers()   const { return m_peers; }

signals:
    // -----------------------------------------------------------------------
    // Qt signals
    // -----------------------------------------------------------------------

    /// Emitted once the join sequence has successfully completed (step 1-7).
    void roomJoined(const RoomInfo& info);

    /// Emitted when a remote peer joins the room after us.
    void peerJoined(const PeerInfo& info);

    /// Emitted when a remote peer leaves.
    void peerLeft(const QString& peerId);

    /// Emitted when the first remote media stream is ready to render.
    void streamReady();

    /// Emitted when any unrecoverable error occurs during setup.
    void connectionFailed(const QString& reason);

private:
    // -----------------------------------------------------------------------
    // Join sequence steps (invoked sequentially via ack callbacks)
    // -----------------------------------------------------------------------

    void step2_loadDevice(const nlohmann::json& ackArgs);
    void step3_createSendTransport();
    void step4_onSendTransportConnected(const nlohmann::json& ackArgs);
    void step6_createRecvTransport();
    void step7_onRecvTransportConnected(const nlohmann::json& ackArgs);

    // -----------------------------------------------------------------------
    // Incoming server-event handlers (registered in joinRoom)
    // -----------------------------------------------------------------------

    void onNewProducer(const nlohmann::json& args);
    void onStreamReady(const nlohmann::json& args);
    void onPeerJoined(const nlohmann::json& args);
    void onPeerLeft(const nlohmann::json& args);

    // -----------------------------------------------------------------------
    // Helpers
    // -----------------------------------------------------------------------

    void failWith(const QString& reason);

    static PeerInfo peerInfoFromJson(const nlohmann::json& obj);

    // -----------------------------------------------------------------------
    // Dependencies (non-owning pointers)
    // -----------------------------------------------------------------------

    SignalingClient* m_signaling{ nullptr };
    MediasoupClient* m_mediasoup{ nullptr };

    // -----------------------------------------------------------------------
    // Room state
    // -----------------------------------------------------------------------

    QString              m_roomId;
    QString              m_displayName;
    QString              m_localPeerId;
    nlohmann::json       m_rtpCapabilities;

    // Pending send-transport params (held between step 3 ack and step 4 connect)
    nlohmann::json       m_sendTransportParams;
    QString              m_sendTransportId;

    // Pending recv-transport params
    nlohmann::json       m_recvTransportParams;
    QString              m_recvTransportId;

    QMap<QString, PeerInfo> m_peers;

    bool m_streamReadyEmitted{ false };
};
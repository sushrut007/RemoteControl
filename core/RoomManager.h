#pragma once

#include <QObject>
#include <QMap>
#include <QString>
#include "AppState.h"

#include <nlohmann/json.hpp>

class SignalingClient;
class P2PClient;

// ---------------------------------------------------------------------------
// RoomManager (P2P / CrossDesk pattern)
//
// Orchestrates P2P room lifecycle:
//  1. signalingClient.emit("join-room")
//        -> ack: { peers, peerId }
//  2. If viewer joins an existing host, viewer signals host:
//        host creates WebRTC offer -> sent via signaling "p2p-signal" (offer)
//        viewer sets remote description -> creates answer -> sends "p2p-signal" (answer)
//  3. ICE candidates exchanged via signaling "p2p-candidate"
//  4. Once P2P connection established -> direct video track + datachannel!
// ---------------------------------------------------------------------------
class RoomManager : public QObject
{
    Q_OBJECT

public:
    explicit RoomManager(SignalingClient* signalingClient,
                         P2PClient* p2pClient,
                         QObject* parent = nullptr);
    ~RoomManager() override;

    RoomManager(const RoomManager&) = delete;
    RoomManager& operator=(const RoomManager&) = delete;

    // -----------------------------------------------------------------------
    // Public API
    // -----------------------------------------------------------------------

    void joinRoom(const QString& roomId,
                  const QString& displayName,
                  const nlohmann::json& metadata = nlohmann::json::object());

    void leaveRoom();

    // -----------------------------------------------------------------------
    // Accessors
    // -----------------------------------------------------------------------

    QString             localPeerId() const { return m_localPeerId; }
    QString             roomId()      const { return m_roomId; }
    QMap<QString, PeerInfo> peers()   const { return m_peers; }

signals:
    void roomJoined(const RoomInfo& info);
    void peerJoined(const PeerInfo& info);
    void peerLeft(const QString& peerId);
    void streamReady();
    void connectionFailed(const QString& reason);

private:
    void onJoinAck(const nlohmann::json& ackArgs);
    void onPeerJoined(const nlohmann::json& args);
    void onPeerLeft(const nlohmann::json& args);
    void onP2pSignal(const nlohmann::json& args);
    void onP2pCandidate(const nlohmann::json& args);
    void onStreamReadySignal(const nlohmann::json& args);
    void onStreamStopped(const nlohmann::json& args);

    // Tears down the current PeerConnection and creates a fresh one.
    // Called before every new negotiation cycle (CrossDesk pattern).
    void resetAndInitP2p();

    // Viewer-side: reinit P2P and emit viewer-ready to the host.
    void startNegotiationAsViewer();

    static PeerInfo peerInfoFromJson(const nlohmann::json& obj);

    SignalingClient* m_signaling{ nullptr };
    P2PClient*       m_p2p{ nullptr };

    QString          m_roomId;
    QString          m_displayName;
    QString          m_localPeerId;
    QString          m_appType;

    QMap<QString, PeerInfo> m_peers;
    bool m_isHost{ false };
    QString m_hostPeerId;            // ID of the host peer (viewer-side tracking)
    bool m_negotiationInProgress{ false }; // Guards against double-negotiation
};

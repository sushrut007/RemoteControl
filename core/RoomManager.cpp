// RoomManager.cpp -- complete, robust P2P lifecycle management
// Mirrors CrossDesk pattern: teardown + reinit PeerConnection before
// every new negotiation cycle so stale state never accumulates.

#include "RoomManager.h"

#include "../network/SignalingClient.h"
#include "../network/P2PClient.h"
#include <QDebug>
#include <QTimer>

RoomManager::RoomManager(SignalingClient* signalingClient,
                         P2PClient* p2pClient,
                         QObject* parent)
    : QObject(parent)
    , m_signaling(signalingClient)
    , m_p2p(p2pClient)
{
    Q_ASSERT(m_signaling);
    Q_ASSERT(m_p2p);

    QObject::connect(m_p2p, &P2PClient::connectionFailed,
                     this, &RoomManager::connectionFailed);

    QObject::connect(m_p2p, &P2PClient::connected, this, [this]() {
        qDebug() << "[RoomManager] P2P connected!";
        emit streamReady();
    });

    QObject::connect(m_p2p, &P2PClient::localDescriptionGenerated,
                     this, [this](const QString& sdp, const QString& type) {
        if (!m_signaling->isConnected()) return;
        qDebug() << "[RoomManager] Sending p2p-signal type:" << type;
        m_signaling->emitEvent(QStringLiteral("p2p-signal"), {
            { "roomId", m_roomId.toStdString() },
            { "type",   type.toStdString() },
            { "sdp",    sdp.toStdString() }
        });
    });

    QObject::connect(m_p2p, &P2PClient::localCandidateGenerated,
                     this, [this](const QString& candidate, const QString& mid) {
        if (!m_signaling->isConnected()) return;
        m_signaling->emitEvent(QStringLiteral("p2p-candidate"), {
            { "roomId",    m_roomId.toStdString() },
            { "candidate", candidate.toStdString() },
            { "mid",       mid.toStdString() }
        });
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
    m_roomId      = roomId;
    m_displayName = displayName;
    m_peers.clear();
    m_hostPeerId.clear();
    m_negotiationInProgress = false;

    m_appType = QStringLiteral("viewer");
    if (metadata.is_object() && metadata.contains("appType") && metadata["appType"].is_string()) {
        m_appType = QString::fromStdString(metadata["appType"].get<std::string>());
    } else {
        m_appType = displayName;
    }
    m_isHost = (m_appType == QLatin1String("host"));

    m_signaling->on(QStringLiteral("peer-joined"),
        [this](const nlohmann::json& args) { onPeerJoined(args); });
    m_signaling->on(QStringLiteral("peer-left"),
        [this](const nlohmann::json& args) { onPeerLeft(args); });
    m_signaling->on(QStringLiteral("p2p-signal"),
        [this](const nlohmann::json& args) { onP2pSignal(args); });
    m_signaling->on(QStringLiteral("p2p-candidate"),
        [this](const nlohmann::json& args) { onP2pCandidate(args); });
    m_signaling->on(QStringLiteral("stream-ready"),
        [this](const nlohmann::json& args) { onStreamReadySignal(args); });
    m_signaling->on(QStringLiteral("stream-stopped"),
        [this](const nlohmann::json& args) { onStreamStopped(args); });
    m_signaling->on(QStringLiteral("viewer-ready"),
        [this](const nlohmann::json& args) {
            if (!m_isHost) return;
            QString viewerPeerId;
            if (!args.empty() && args[0].is_object() && args[0].contains("peerId")) {
                viewerPeerId = QString::fromStdString(args[0]["peerId"].get<std::string>());
            }
            qDebug() << "[RoomManager] viewer-ready from" << viewerPeerId
                     << "-- reinitialising P2P and creating offer";
            resetAndInitP2p();
            QTimer::singleShot(50, this, [this]() { m_p2p->createOffer(); });
        });

    resetAndInitP2p();

    m_signaling->emitEvent(
        QStringLiteral("join-room"),
        nlohmann::json{
            { "roomId",      roomId.toStdString() },
            { "displayName", displayName.toStdString() },
            { "appType",     m_appType.toStdString() },
            { "metadata",    metadata }
        },
        [this](const nlohmann::json& ackArgs) { onJoinAck(ackArgs); });
}

void RoomManager::leaveRoom()
{
    if (m_signaling->isConnected()) {
        m_signaling->emitEvent(QStringLiteral("leave-room"), nlohmann::json::object());
    }
    m_p2p->close();
    m_peers.clear();
    m_localPeerId.clear();
    m_roomId.clear();
    m_hostPeerId.clear();
    m_negotiationInProgress = false;
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void RoomManager::resetAndInitP2p()
{
    const AppSettings s = APP_STATE->appSettings();
    m_p2p->init(m_isHost, s.stunServer, s.turnServer, s.turnUsername, s.turnPassword);
    m_negotiationInProgress = false;
}

void RoomManager::startNegotiationAsViewer()
{
    if (m_negotiationInProgress) {
        qDebug() << "[RoomManager] Viewer: negotiation already in progress, skipping";
        return;
    }
    m_negotiationInProgress = true;
    // Full P2P reinit so we always present a fresh RecvOnly track in the SDP.
    resetAndInitP2p();
    m_negotiationInProgress = true; // resetAndInitP2p clears it; restore.
    qDebug() << "[RoomManager] Viewer: sending viewer-ready to trigger host offer";
    m_signaling->emitEvent(QStringLiteral("viewer-ready"), {
        { "roomId", m_roomId.toStdString() }
    });
}

// ---------------------------------------------------------------------------
// Join acknowledgement
// ---------------------------------------------------------------------------

void RoomManager::onJoinAck(const nlohmann::json& ackArgs)
{
    if (ackArgs.empty()) {
        emit connectionFailed(QStringLiteral("Empty join acknowledgement from server"));
        return;
    }

    const nlohmann::json& resp = ackArgs.is_array() ? ackArgs[0] : ackArgs;

    if (resp.contains("error") && resp["error"].is_string()) {
        emit connectionFailed(QString::fromStdString(resp["error"].get<std::string>()));
        return;
    }

    if (resp.contains("peerId") && resp["peerId"].is_string()) {
        m_localPeerId = QString::fromStdString(resp["peerId"].get<std::string>());
    }

    RoomInfo info;
    info.roomId      = m_roomId;
    info.localPeerId = m_localPeerId;
    info.streamReady = false;

    bool hostAlreadyPresent = false;

    if (resp.contains("peers") && resp["peers"].is_array()) {
        for (const auto& p : resp["peers"]) {
            PeerInfo pi = peerInfoFromJson(p);
            m_peers.insert(pi.id, pi);
            info.peers.append(pi);
            if (pi.appType == QLatin1String("host")) {
                m_hostPeerId = pi.id;
                hostAlreadyPresent = true;
            }
        }
    }

    emit roomJoined(info);

    // Emit peerJoined for pre-existing peers so the UI reflects them.
    for (const PeerInfo& existing : qAsConst(info.peers)) {
        emit peerJoined(existing);
    }

    if (m_isHost) {
        qDebug() << "[RoomManager] Host joined room.";
    } else {
        if (hostAlreadyPresent) {
            qDebug() << "[RoomManager] Viewer joined, host already present -- starting P2P";
            startNegotiationAsViewer();
        } else {
            qDebug() << "[RoomManager] Viewer joined, no host yet -- waiting for peer-joined";
        }
    }
}

// ---------------------------------------------------------------------------
// Peer events
// ---------------------------------------------------------------------------

void RoomManager::onPeerJoined(const nlohmann::json& args)
{
    if (args.empty()) return;
    PeerInfo peer = peerInfoFromJson(args[0]);
    m_peers.insert(peer.id, peer);
    emit peerJoined(peer);

    if (m_isHost) {
        qDebug() << "[RoomManager] Host: peer joined (" << peer.id
                 << "type=" << peer.appType << "). Awaiting viewer-ready.";
    } else {
        if (peer.appType == QLatin1String("host")) {
            m_hostPeerId = peer.id;
            qDebug() << "[RoomManager] Viewer: host joined (" << peer.id << ") -- starting P2P";
            startNegotiationAsViewer();
        }
    }
}

void RoomManager::onPeerLeft(const nlohmann::json& args)
{
    if (args.empty()) return;
    QString peerId;
    if (args[0].is_object() && args[0].contains("peerId")) {
        peerId = QString::fromStdString(args[0]["peerId"].get<std::string>());
    } else if (args[0].is_string()) {
        peerId = QString::fromStdString(args[0].get<std::string>());
    }
    m_peers.remove(peerId);

    if (peerId == m_hostPeerId) {
        qDebug() << "[RoomManager] Host left -- closing P2P, ready for rejoin";
        m_hostPeerId.clear();
        m_negotiationInProgress = false;
        m_p2p->close();
    }

    emit peerLeft(peerId);
}

// ---------------------------------------------------------------------------
// P2P signal relay
// ---------------------------------------------------------------------------

void RoomManager::onP2pSignal(const nlohmann::json& args)
{
    if (args.empty() || !args[0].is_object()) return;
    const auto& data = args[0];
    std::string type = data.value("type", "");
    std::string sdp  = data.value("sdp", "");
    if (sdp.empty() || type.empty()) return;

    qDebug() << "[RoomManager] Received p2p-signal type:" << QString::fromStdString(type);

    if (!m_isHost && type == "offer" && !m_negotiationInProgress) {
        qDebug() << "[RoomManager] Viewer: unsolicited offer -- reiniting P2P";
        resetAndInitP2p();
        m_negotiationInProgress = true;
    }

    m_p2p->setRemoteDescription(QString::fromStdString(sdp),
                                QString::fromStdString(type));
}

void RoomManager::onP2pCandidate(const nlohmann::json& args)
{
    if (args.empty() || !args[0].is_object()) return;
    const auto& data = args[0];
    std::string candidate = data.value("candidate", "");
    std::string mid       = data.value("mid", "");
    if (!candidate.empty()) {
        m_p2p->addRemoteCandidate(QString::fromStdString(candidate),
                                  QString::fromStdString(mid));
    }
}

// ---------------------------------------------------------------------------
// Stream events
// ---------------------------------------------------------------------------

void RoomManager::onStreamReadySignal(const nlohmann::json& /*args*/)
{
    emit streamReady();
}

void RoomManager::onStreamStopped(const nlohmann::json& /*args*/)
{
}

// ---------------------------------------------------------------------------
// JSON helper
// ---------------------------------------------------------------------------

PeerInfo RoomManager::peerInfoFromJson(const nlohmann::json& obj)
{
    PeerInfo p;
    if (!obj.is_object()) return p;
    p.id          = QString::fromStdString(obj.value("peerId", obj.value("id", "")));
    p.displayName = QString::fromStdString(obj.value("displayName", ""));
    p.appType     = QString::fromStdString(obj.value("appType", "viewer"));
    p.role        = QString::fromStdString(obj.value("role", ""));
    p.joinedAt    = QDateTime::currentDateTime();
    if (obj.contains("metadata") && obj["metadata"].is_object()) {
        p.metadata = obj["metadata"];
        if (p.appType.isEmpty() && p.metadata.contains("appType")) {
            p.appType = QString::fromStdString(
                p.metadata["appType"].get<std::string>());
        }
    }
    return p;
}

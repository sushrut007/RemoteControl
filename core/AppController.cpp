#include "AppController.h"

#include "../network/SignalingClient.h"
#include "../network/P2PClient.h"
#include "../input/InputInjector.h"
#include "RoomManager.h"
#include "ScreenCapturer.h"
#include "../mediasoup/VideoProducer.h"
#include "../mediasoup/VideoDecoder.h"
#include "ControlEventHandler.h"
#include "../ui/AppShell.h"
#include "../ui/ConnectModal.h"
#include "../ui/ViewerPage.h"
#include "../ui/HostPage.h"
#include "../ui/SettingsModal.h"

#include <nlohmann/json.hpp>

#include <QMetaObject>
#include <QTimer>
#include <QApplication>

// ===========================================================================
// Construction / destruction
// ===========================================================================

AppController::AppController(AppShell* shell, QObject* parent)
    : QObject(parent)
    , m_shell(shell)
{
    Q_ASSERT(shell);

    m_injector = new InputInjector(this);
    m_controlHandler = new ControlEventHandler(m_injector, this);
    m_p2p = new P2PClient(this);
    m_signaling = new SignalingClient(this);
    m_roomManager = new RoomManager(m_signaling, m_p2p, this);
    m_capturer = new ScreenCapturer(this);
    m_producer = new VideoProducer(this);
    m_decoder = new VideoDecoder(this);

    m_connectModal = shell->findChild<ConnectModal*>();
    m_viewerPage = shell->findChild<ViewerPage*>();
    m_hostPage = shell->findChild<HostPage*>();

    wireSignalingClient();
    wireRoomManager();
    wireP2PClient();
    wireScreenCapturer();
    wireVideoProducer();
    wireVideoDecoder();
    wireViewerPage();
    wireHostPage();
    wireAppShell();
}

AppController::~AppController() = default;

void AppController::start()
{
    m_shell->showPage(PageType::Connect);
    if (m_connectModal) {
        m_shell->showModal(m_connectModal);
    }
}

// ===========================================================================
// Wiring helpers
// ===========================================================================

void AppController::wireSignalingClient()
{
    QObject::connect(m_signaling, &SignalingClient::connected,
        this, &AppController::onSignalingConnected,
        Qt::QueuedConnection);

    QObject::connect(m_signaling, &SignalingClient::disconnected,
        this, &AppController::onSignalingDisconnected,
        Qt::QueuedConnection);

    QObject::connect(m_signaling, &SignalingClient::authError,
        this, &AppController::onSignalingAuthError,
        Qt::QueuedConnection);

    QObject::connect(m_signaling, &SignalingClient::signalingLog,
        this, [this](const QString& msg) {
            if (m_connectModal) {
                QMetaObject::invokeMethod(m_connectModal,
                    [this, msg]() { m_connectModal->appendLog(msg); },
                    Qt::QueuedConnection);
            }
        }, Qt::QueuedConnection);

    // Stream-stopped overlay notification
    m_signaling->on(QStringLiteral("stream-stopped"),
        [this](const nlohmann::json&) {
            QMetaObject::invokeMethod(this, [this]() {
                if (m_pendingConfig.appType == QLatin1String("host")) { return; }
                m_decoder->stop();
                if (m_viewerPage) {
                    m_viewerPage->showWaitingOverlay(
                        QStringLiteral("Host paused the stream.\nWaiting to resume…"));
                }
            }, Qt::QueuedConnection);
        });
}

void AppController::wireP2PClient()
{
    // When P2P becomes connected, force a keyframe (host) or hide overlay (viewer)
    QObject::connect(m_p2p, &P2PClient::connected,
        this, [this]() {
            if (m_pendingConfig.appType == QLatin1String("host")) {
                // Force IDR/keyframe so viewer decoder gets SPS+PPS immediately
                if (m_producer && m_producer->isRunning()) {
                    qDebug() << "[AppController] P2P connected — forcing keyframe for viewer";
                    m_producer->forceKeyframe();
                }
            } else {
                // Hide waiting overlay once P2P stream is live
                if (m_viewerPage) {
                    QMetaObject::invokeMethod(m_viewerPage, [this]() {
                        m_viewerPage->hideWaitingOverlay();
                    }, Qt::QueuedConnection);
                }
            }
        }, Qt::QueuedConnection);

    // Receive direct P2P video frames on viewer side
    QObject::connect(m_p2p, &P2PClient::videoFrameReceived,
        this, [this](const QByteArray& nalu, bool isKeyframe) {
            if (m_pendingConfig.appType == QLatin1String("host")) return;
            if (!m_decoder->isRunning()) {
                m_decoder->start();
            }
            m_decoder->decodePacket(nalu, isKeyframe);
        }, Qt::DirectConnection);

    // Receive direct P2P control messages on host side
    QObject::connect(m_p2p, &P2PClient::controlMessageReceived,
        this, [this](const nlohmann::json& payload) {
            if (m_pendingConfig.appType != QLatin1String("host")) return;
            if (!payload.is_object()) return;
            const std::string evType = payload.value("type", "");
            if (evType == "mouse") {
                m_controlHandler->handleMouse(payload);
            } else if (evType == "keyboard") {
                m_controlHandler->handleKeyboard(payload);
            } else {
                m_controlHandler->handleControl(payload);
            }
        }, Qt::QueuedConnection);
}

void AppController::wireRoomManager()
{
    QObject::connect(m_roomManager, &RoomManager::roomJoined,
        this, [this](const RoomInfo& info) {
            RoomInfo stateInfo;
            stateInfo.roomId = info.roomId;
            stateInfo.streamReady = false;
            APP_STATE->setRoomInfo(stateInfo);
            onRoomJoined();
        }, Qt::QueuedConnection);

    QObject::connect(m_roomManager, &RoomManager::peerJoined,
        this, [this](const PeerInfo& peer) {
            const QString appType = !peer.appType.isEmpty()
                ? peer.appType
                : (peer.metadata.is_object()
                    ? QString::fromStdString(peer.metadata.value("appType", "viewer"))
                    : QStringLiteral("viewer"));
            onPeerJoined(peer.id, appType);
        }, Qt::QueuedConnection);

    QObject::connect(m_roomManager, &RoomManager::peerLeft,
        this, &AppController::onPeerLeft,
        Qt::QueuedConnection);

    QObject::connect(m_roomManager, &RoomManager::streamReady,
        this, &AppController::onStreamReady,
        Qt::QueuedConnection);

    QObject::connect(m_roomManager, &RoomManager::connectionFailed,
        this, &AppController::onConnectionFailed,
        Qt::QueuedConnection);
}

void AppController::wireScreenCapturer()
{
    QObject::connect(m_capturer, &ScreenCapturer::frameReady,
        m_producer, &VideoProducer::onFrame,
        Qt::QueuedConnection);

    QObject::connect(m_capturer, &ScreenCapturer::frameReady,
        this, [this](const QImage& frame) {
            if (m_hostPage) {
                m_hostPage->updatePreviewFrame(frame);
            }
        }, Qt::QueuedConnection);

    QObject::connect(m_capturer, &ScreenCapturer::captureError,
        this, &AppController::onCaptureError,
        Qt::QueuedConnection);

    QObject::connect(m_capturer, &ScreenCapturer::captureRegionReady,
        this, [this](int /*originX*/, int /*originY*/, int width, int height) {
            const AppSettings s = APP_STATE->appSettings();
            if (m_producer->isRunning()) {
                m_producer->stop();
            }
            m_producer->start(width, height, s.targetFps, s.bitrateKbps);
        }, Qt::QueuedConnection);

    QObject::connect(m_capturer, &ScreenCapturer::captureRegionReady,
        m_injector, &InputInjector::setCaptureRegion,
        Qt::QueuedConnection);
}

void AppController::wireVideoProducer()
{
    QObject::connect(m_signaling, &SignalingClient::networkRttMeasured,
        m_producer, &VideoProducer::notifyRtt,
        Qt::QueuedConnection);

    // Send encoded H.264 packets directly through P2PClient video track!
    QObject::connect(m_producer, &VideoProducer::packetReady,
        this, [this](const QByteArray& data, bool isKeyframe) {
            if (m_p2p && m_p2p->isConnected()) {
                m_p2p->sendVideoFrame(data, isKeyframe);
            }
        }, Qt::DirectConnection);

    QObject::connect(m_producer, &VideoProducer::statsUpdated,
        this, [this](const VideoStats& s) {
            onVideoStatsUpdated(s.fps, s.bitrate, s.width, s.height);
        }, Qt::QueuedConnection);

    QObject::connect(m_producer, &VideoProducer::encodingError,
        this, &AppController::onEncodingError,
        Qt::QueuedConnection);
}

void AppController::wireVideoDecoder()
{
    QObject::connect(m_decoder, &VideoDecoder::frameReady,
        this, [this](const QImage& frame) {
            if (m_viewerPage) {
                m_viewerPage->renderer()->uploadFrame(frame);
            }
        }, Qt::DirectConnection);

    QObject::connect(m_decoder, &VideoDecoder::frameReady,
        this, [this](const QImage& /*frame*/) {
            if (m_viewerPage) {
                m_viewerPage->onFrameDelivered();
            }
        }, Qt::QueuedConnection);

    QObject::connect(m_decoder, &VideoDecoder::decodeError,
        this, [](const QString& msg) {
            qWarning() << "VideoDecoder error:" << msg;
        }, Qt::QueuedConnection);
}

void AppController::wireViewerPage()
{
    if (!m_viewerPage) { return; }

    QObject::connect(m_viewerPage, &ViewerPage::mouseEvent,
        this, [this](const MouseData& d) {
            onViewerMouseEvent(d.x, d.y, d.deltaX, d.deltaY, d.type, d.button, {});
        }, Qt::DirectConnection);

    QObject::connect(m_viewerPage, &ViewerPage::keyboardEvent,
        this, [this](const KeyboardData& d) {
            onViewerKeyboardEvent(d.key, d.type, d.modifiers);
        }, Qt::DirectConnection);

    QObject::connect(m_viewerPage, &ViewerPage::disconnectRequested,
        this, &AppController::onViewerDisconnectRequested,
        Qt::QueuedConnection);
}

void AppController::wireHostPage()
{
    if (!m_hostPage) { return; }

    QObject::connect(m_hostPage, &HostPage::shareToggled,
        this, &AppController::onShareToggled,
        Qt::QueuedConnection);

    QObject::connect(m_hostPage, &HostPage::controlToggled,
        this, &AppController::onControlToggled,
        Qt::QueuedConnection);

    QObject::connect(m_hostPage, &HostPage::kickPeer,
        this, &AppController::onKickPeer,
        Qt::QueuedConnection);

    QObject::connect(m_hostPage, &HostPage::sessionEnded,
        this, &AppController::onSessionEnded,
        Qt::QueuedConnection);
}

void AppController::wireAppShell()
{
    QObject::connect(m_shell, &AppShell::settingsRequested,
        this, &AppController::onSettingsRequested,
        Qt::QueuedConnection);

    QObject::connect(m_shell, &AppShell::disconnectRequested,
        this, &AppController::onDisconnectRequested,
        Qt::QueuedConnection);

    QObject::connect(m_shell, &AppShell::quitRequested,
        qApp, &QCoreApplication::quit,
        Qt::QueuedConnection);

    if (m_connectModal) {
        QObject::connect(m_connectModal, &ConnectModal::connectRequested,
            this, [this](const ConnectionConfig& cfg) {
                onConnectRequested(cfg);
            }, Qt::QueuedConnection);

        QObject::connect(m_connectModal, &ConnectModal::cancelled,
            m_shell, &AppShell::hideModal,
            Qt::QueuedConnection);

        QObject::connect(m_connectModal, &ConnectModal::sizeChanged,
            m_shell, &AppShell::recenterModal,
            Qt::QueuedConnection);
    }
}

// ===========================================================================
// Slots – ConnectModal
// ===========================================================================

void AppController::onConnectRequested(const ConnectionConfig& cfg)
{
    m_pendingConfig = cfg;
    APP_STATE->setConnectionConfig(cfg);
    APP_STATE->setConnectionState(AppState::ConnectionState::Connecting);

    m_shell->setConnectionStatus(AppShell::ConnectionStatus::Connecting);
    if (m_connectModal) { m_connectModal->setLoading(true); }

    m_signaling->connectToServer(cfg.serverUrl);
}

// ===========================================================================
// Slots – SignalingClient
// ===========================================================================

void AppController::onSignalingConnected()
{
    m_roomManager->joinRoom(
        m_pendingConfig.roomId,
        m_pendingConfig.appType,
        nlohmann::json{
            { "appType",  m_pendingConfig.appType.toStdString() },
            { "password", m_pendingConfig.password.toStdString() }
        });
}

void AppController::onSignalingDisconnected()
{
    APP_STATE->setConnectionState(AppState::ConnectionState::Disconnected);
    m_shell->setConnectionStatus(AppShell::ConnectionStatus::Disconnected);
    stopCapture();
}

void AppController::onSignalingAuthError(const QString& reason)
{
    APP_STATE->setConnectionState(AppState::ConnectionState::Error);
    m_shell->setConnectionStatus(AppShell::ConnectionStatus::Disconnected);

    if (m_connectModal) {
        m_connectModal->setLoading(false);
        m_connectModal->setStatusMessage(reason, true);
    }
}

// ===========================================================================
// Slots – RoomManager
// ===========================================================================

void AppController::onRoomJoined()
{
    APP_STATE->setConnectionState(AppState::ConnectionState::Connected);
    m_shell->setConnectionStatus(AppShell::ConnectionStatus::Connected);
    m_shell->hideModal();

    const QString appType = m_pendingConfig.appType;
    m_shell->setSessionInfo(m_pendingConfig.roomId, appType);

    if (appType == QLatin1String("host")) {
        m_shell->showPage(PageType::Host);
        if (m_hostPage) {
            m_hostPage->setRoomInfo(m_pendingConfig.roomId,
                m_pendingConfig.password,
                m_pendingConfig.serverUrl);
        }
        m_controlHandler->setActiveRoom(m_pendingConfig.roomId, false);

        if (m_sharingActive) {
            startCapture();
            m_signaling->emitEvent(QStringLiteral("stream-ready"), nlohmann::json::object());
        }
    } else {
        m_shell->showPage(PageType::Viewer);
        m_decoder->stop();

        const AppSettings s = APP_STATE->appSettings();
        m_decoder->setFps(s.targetFps);
        m_decoder->start();

        if (m_viewerPage) {
            m_viewerPage->setSessionRole(appType);
            m_viewerPage->setRoomId(m_pendingConfig.roomId);
            m_viewerPage->showWaitingOverlay(
                QStringLiteral("Waiting for host to connect..."));
        }
    }
}

void AppController::onPeerJoined(const QString& peerId, const QString& appType)
{
    PeerInfo statePeer;
    statePeer.id = peerId;
    statePeer.appType = appType;
    statePeer.joinedAt = QDateTime::currentDateTime();
    APP_STATE->addPeer(statePeer);

    m_controlHandler->addPeer(peerId, appType);

    if (appType == QLatin1String("controller") && m_controlAllowedByHost) {
        m_controlHandler->setEnabled(true);
        m_controlHandler->setActiveRoom(m_pendingConfig.roomId, true);
    }

    if (appType == QLatin1String("host")) {
        m_hostPeerId = peerId;
        if (m_viewerPage && m_pendingConfig.appType != QLatin1String("host")) {
            m_viewerPage->showWaitingOverlay(
                QStringLiteral("Host connected – establishing P2P stream..."));
        }
    }

    if (m_hostPage) {
        m_hostPage->addPeer(statePeer);
        m_hostPage->setStreamStatus(m_capturer->isRunning(),
            APP_STATE->roomInfo().peers.size());
    }

    if (m_pendingConfig.appType == QLatin1String("host") && m_producer->isRunning()) {
        m_producer->forceKeyframe();
    }
}

void AppController::onPeerLeft(const QString& peerId)
{
    APP_STATE->removePeer(peerId);
    m_controlHandler->removePeer(peerId);

    if (peerId == m_hostPeerId && m_pendingConfig.appType != QLatin1String("host")) {
        m_hostPeerId.clear();
        m_decoder->stop();
        if (m_viewerPage) {
            m_viewerPage->showWaitingOverlay(
                QStringLiteral("Host disconnected.\nWaiting for host to rejoin..."));
        }
    }

    if (m_hostPage) {
        m_hostPage->removePeer(peerId);
        m_hostPage->setStreamStatus(m_capturer->isRunning(),
            APP_STATE->roomInfo().peers.size());
    }
}

void AppController::onStreamReady()
{
    RoomInfo info = APP_STATE->roomInfo();
    info.streamReady = true;
    APP_STATE->setRoomInfo(info);

    if (m_pendingConfig.appType == QLatin1String("host")) {
        startCapture();
        return;
    }

    const AppSettings s = APP_STATE->appSettings();
    m_decoder->setFps(s.targetFps);
    if (!m_decoder->isRunning()) {
        m_decoder->start();
    }
    // Do NOT show the "Receiving P2P stream" overlay here.
    // The overlay was already hidden when the P2P connection became active.
    // The viewer will see the first video frame as soon as the decoder produces
    // one; ViewerPage::updateFrame() / onFrameDelivered() handles the
    // overlay-to-renderer transition automatically.
}

void AppController::onConnectionFailed(const QString& reason)
{
    APP_STATE->setConnectionState(AppState::ConnectionState::Error);
    m_shell->setConnectionStatus(AppShell::ConnectionStatus::Disconnected);

    if (m_connectModal) {
        m_connectModal->setLoading(false);
        m_connectModal->setStatusMessage(reason, true);
        m_shell->showModal(m_connectModal);
    }
    stopCapture();
}

void AppController::onCaptureError(const QString& msg)
{
    Q_UNUSED(msg)
}

void AppController::onVideoStatsUpdated(double fps, int bitrateKbps, int w, int h)
{
    VideoStats s;
    s.fps = static_cast<int>(fps);
    s.bitrate = bitrateKbps;
    s.width = w;
    s.height = h;
    s.latency = static_cast<double>(m_signaling->lastRttMs());
    APP_STATE->setVideoStats(s);

    if (m_viewerPage) {
        ConnectionInfo info;
        info.fps = fps;
        info.bitrateKbps = bitrateKbps;
        info.width = w;
        info.height = h;
        info.latencyMs = m_signaling->lastRttMs();
        m_viewerPage->setConnectionInfo(info);
    }
}

void AppController::onEncodingError(const QString& /*msg*/)
{
}

// ===========================================================================
// Slots – ViewerPage (P2P DataChannel mouse & keyboard dispatch)
// ===========================================================================

void AppController::onViewerMouseEvent(float x, float y, float dx, float dy,
    const QString& type,
    const QString& button,
    const QStringList& /*mods*/)
{
    if (m_pendingConfig.appType != QLatin1String("controller")) { return; }
    if (!m_p2p || !m_p2p->isConnected()) { return; }

    nlohmann::json payload = {
        { "type",      "mouse" },
        { "x",         x },
        { "y",         y },
        { "deltaX",    dx },
        { "deltaY",    dy },
        { "eventType", type.toStdString() },
        { "button",    button.toStdString() },
        { "senderId",  m_roomManager->localPeerId().toStdString() }
    };
    m_p2p->sendControlMessage(payload);
}

void AppController::onViewerKeyboardEvent(const QString& key,
    const QString& type,
    const QStringList& mods)
{
    if (m_pendingConfig.appType != QLatin1String("controller")) { return; }
    if (!m_p2p || !m_p2p->isConnected()) { return; }

    nlohmann::json modArr = nlohmann::json::array();
    for (const QString& m : mods) { modArr.push_back(m.toStdString()); }

    nlohmann::json payload = {
        { "type",      "keyboard" },
        { "key",       key.toStdString() },
        { "eventType", type.toStdString() },
        { "modifiers", modArr },
        { "senderId",  m_roomManager->localPeerId().toStdString() }
    };
    m_p2p->sendControlMessage(payload);
}

void AppController::onViewerDisconnectRequested()
{
    onDisconnectRequested();
}

// ===========================================================================
// Slots – HostPage
// ===========================================================================

void AppController::onShareToggled(bool active)
{
    m_sharingActive = active;
    if (active) {
        startCapture();
        m_producer->forceKeyframe();
        if (m_signaling->isConnected()) {
            m_signaling->emitEvent(QStringLiteral("stream-ready"), nlohmann::json::object());
        }
    } else {
        stopCapture();
        if (m_signaling->isConnected()) {
            m_signaling->emitEvent(QStringLiteral("stream-stopped"), nlohmann::json::object());
        }
    }
    if (m_hostPage) {
        m_hostPage->setStreamStatus(active, APP_STATE->roomInfo().peers.size());
    }
}

void AppController::onControlToggled(bool allowed)
{
    m_controlAllowedByHost = allowed;
    m_controlHandler->setEnabled(allowed);
    m_controlHandler->setActiveRoom(m_pendingConfig.roomId, allowed);
}

void AppController::onKickPeer(const QString& peerId)
{
    if (!m_signaling->isConnected()) { return; }
    m_signaling->emitEvent(QStringLiteral("kick-peer"), {
        { "peerId", peerId.toStdString() }
    });
}

void AppController::onSessionEnded()
{
    teardownSession();
    m_shell->showPage(PageType::Connect);
    if (m_connectModal) {
        m_connectModal->setLoading(false);
        m_connectModal->setStatusMessage(QString());
        m_shell->showModal(m_connectModal);
    }
}

// ===========================================================================
// Slots – AppShell
// ===========================================================================

void AppController::onSettingsRequested()
{
    auto* dlg = new SettingsModal(m_shell);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    QObject::connect(dlg, &SettingsModal::settingsChanged,
        this, [this](const AppSettings& s) {
            APP_STATE->setAppSettings(s);
            m_capturer->setQuality(s.previewQuality);
        }, Qt::QueuedConnection);
    dlg->exec();
}

void AppController::onDisconnectRequested()
{
    teardownSession();
    m_shell->showPage(PageType::Connect);
    if (m_connectModal) {
        m_connectModal->setLoading(false);
        m_connectModal->setStatusMessage(QString());
        m_shell->showModal(m_connectModal);
    }
}

// ===========================================================================
// Internal helpers
// ===========================================================================

void AppController::startCapture()
{
    const AppSettings s = APP_STATE->appSettings();
    if (!m_capturer->isRunning()) {
        m_capturer->start(s.monitorIndex, s.targetFps);
    }
}

void AppController::stopCapture()
{
    m_capturer->stop();
    m_producer->stop();
}

void AppController::teardownSession()
{
    m_sharingActive = false;
    m_controlAllowedByHost = false;
    m_hostPeerId.clear();

    if (m_pendingConfig.appType == QLatin1String("host") && m_signaling->isConnected()) {
        m_signaling->emitEvent(QStringLiteral("stream-stopped"), nlohmann::json::object());
    }

    stopCapture();
    m_decoder->stop();
    m_p2p->close();
    m_roomManager->leaveRoom();
    m_signaling->disconnect();
    m_controlHandler->setEnabled(false);

    APP_STATE->setConnectionState(AppState::ConnectionState::Disconnected);
    APP_STATE->setRoomInfo({});
    m_shell->setConnectionStatus(AppShell::ConnectionStatus::Disconnected);
    m_shell->setSessionInfo(QString(), QString());
}

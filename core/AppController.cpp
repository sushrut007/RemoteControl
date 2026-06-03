#include "AppController.h"

#include "../network/SignalingClient.h"
#include "../mediasoup/MediasoupClient.h"
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
    m_mediasoup = new MediasoupClient(this);
    m_signaling = new SignalingClient(this);
    m_roomManager = new RoomManager(m_signaling, m_mediasoup, this);
    m_capturer = new ScreenCapturer(this);
    m_producer = new VideoProducer(this);
    m_decoder = new VideoDecoder(this);

    m_connectModal = shell->findChild<ConnectModal*>();
    m_viewerPage = shell->findChild<ViewerPage*>();
    m_hostPage = shell->findChild<HostPage*>();

    wireSignalingClient();
    wireRoomManager();
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
    // Receive and dispatch remote control events (mouse / keyboard / command).
    // The payload's top-level "type" field determines the sub-handler.
    m_signaling->on(QStringLiteral("control"),
        [this](const nlohmann::json& args) {
            const nlohmann::json& payload =
                args.is_array() && !args.empty() ? args[0] : args;
            QMetaObject::invokeMethod(this, [this, payload]() {
                const std::string evType = payload.value("type", "");
                if (evType == "mouse") {
                    m_controlHandler->handleMouse(payload);
                }
                else if (evType == "keyboard") {
                    m_controlHandler->handleKeyboard(payload);
                }
                else {
                    m_controlHandler->handleControl(payload);
                }
                }, Qt::QueuedConnection);
        });

    // Receive encoded video frames forwarded by the server.
    // Only non-host peers (viewer / controller) need to decode and display them.
    m_signaling->on(QStringLiteral("video-packet"),
        [this](const nlohmann::json& args) {
            const nlohmann::json& pkt =
                args.is_array() && !args.empty() ? args[0] : args;
            if (!pkt.contains("data")) { return; }
            const std::string b64 = pkt["data"].get<std::string>();
            const bool isKf = pkt.value("isKeyframe", false);
            QMetaObject::invokeMethod(this, [this, b64, isKf]() {
                if (m_pendingConfig.appType == QLatin1String("host")) { return; }
                const QByteArray raw =
                    QByteArray::fromBase64(QByteArray::fromStdString(b64));
                if (!m_decoder->isRunning()) { m_decoder->start(); }
                m_decoder->decodePacket(raw, isKf);
                }, Qt::QueuedConnection);
        });

    // Host stopped sharing – clear the frozen frame and show a status message.
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

    // This peer was kicked by the host – tear down and go back to connect screen.
    m_signaling->on(QStringLiteral("kicked"),
        [this](const nlohmann::json&) {
            QMetaObject::invokeMethod(this, [this]() {
                teardownSession();
                m_shell->showPage(PageType::Connect);
                if (m_connectModal) {
                    m_connectModal->setLoading(false);
                    m_connectModal->setStatusMessage(
                        QStringLiteral("You were removed from the session by the host."),
                        /*isError=*/true);
                    m_shell->showModal(m_connectModal);
                }
                }, Qt::QueuedConnection);
        });
}

void AppController::wireRoomManager()
{
    QObject::connect(m_roomManager, &RoomManager::roomJoined,
        this, [this](const RoomInfo& info) {
            RoomInfo stateInfo;
            stateInfo.roomId = info.roomId;
            stateInfo.streamReady = false;
            APP_STATE->setRoomInfo(stateInfo);
            QMetaObject::invokeMethod(this, &AppController::onRoomJoined,
                Qt::QueuedConnection);
        }, Qt::QueuedConnection);

    QObject::connect(m_roomManager, &RoomManager::peerJoined,
        this, [this](const PeerInfo& peer) {
            // appType is now set directly on PeerInfo by peerInfoFromJson;
  // fall back to metadata for older server versions.
            const QString appType = !peer.appType.isEmpty()
                ? peer.appType
                : QString::fromStdString(peer.metadata.value("appType", "viewer"));
            const QString id = peer.id;
            QMetaObject::invokeMethod(this,
                [this, id, appType]() {
                    onPeerJoined(id, appType);
                }, Qt::QueuedConnection);
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
                QMetaObject::invokeMethod(m_hostPage,
                    [this, frame]() { m_hostPage->updatePreviewFrame(frame); },
                    Qt::QueuedConnection);
            }
        }, Qt::QueuedConnection);

    QObject::connect(m_capturer, &ScreenCapturer::captureError,
        this, &AppController::onCaptureError,
        Qt::QueuedConnection);

    // When the captured monitor's region is known, inform InputInjector so it
    // can map normalised controller coordinates to the correct virtual-desktop
    // position (needed for multi-monitor setups).
    QObject::connect(m_capturer, &ScreenCapturer::captureRegionReady,
        m_injector, &InputInjector::setCaptureRegion,
        Qt::QueuedConnection);
}

void AppController::wireVideoProducer()
{
    QObject::connect(m_producer, &VideoProducer::packetReady,
        this, [this](const QByteArray& data, bool isKeyframe) {
            if (!m_signaling->isConnected()) { return; }
            const QByteArray captured = data;
            const bool       kf = isKeyframe;
            QMetaObject::invokeMethod(this, [this, captured, kf]() {
                m_signaling->emitEvent(QStringLiteral("video-packet"), {
                    { "data",       captured.toBase64().toStdString() },
                    { "isKeyframe", kf }
                    });
                }, Qt::QueuedConnection);
        }, Qt::QueuedConnection);

    QObject::connect(m_producer, &VideoProducer::statsUpdated,
        this, [this](const VideoStats& s) {
            const int  fps = s.fps;
            const int  bps = s.bitrate;
            const int  w = s.width;
            const int  h = s.height;
            QMetaObject::invokeMethod(this,
                [this, fps, bps, w, h]() {
                    onVideoStatsUpdated(fps, bps, w, h);
                }, Qt::QueuedConnection);
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
                QMetaObject::invokeMethod(m_viewerPage,
                    [this, frame]() { m_viewerPage->updateFrame(frame); },
                    Qt::QueuedConnection);
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
            const float   x = d.x, y = d.y;
            const float   dx = d.deltaX, dy = d.deltaY;
            const QString t = d.type, b = d.button;
            QMetaObject::invokeMethod(this,
                [this, x, y, dx, dy, t, b]() {
                    onViewerMouseEvent(x, y, dx, dy, t, b, {});
                }, Qt::QueuedConnection);
        }, Qt::QueuedConnection);

    QObject::connect(m_viewerPage, &ViewerPage::keyboardEvent,
        this, [this](const KeyboardData& d) {
            const QString k = d.key, t = d.type;
            const QStringList m = d.modifiers;
            QMetaObject::invokeMethod(this,
                [this, k, t, m]() {
                    onViewerKeyboardEvent(k, t, m);
                }, Qt::QueuedConnection);
        }, Qt::QueuedConnection);

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
                const ConnectionConfig captured = cfg;
                QMetaObject::invokeMethod(this,
                    [this, captured]() { onConnectRequested(captured); },
                    Qt::QueuedConnection);
            }, Qt::QueuedConnection);

        QObject::connect(m_connectModal, &ConnectModal::cancelled,
            m_shell, &AppShell::hideModal,
            Qt::QueuedConnection);

        // When the log panel appears/disappears the dialog changes height;
        // re-center it in the overlay.
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
            { "appType",  m_pendingConfig.appType.toStdString()    },
            { "password", m_pendingConfig.password.toStdString()   }
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
        m_connectModal->setStatusMessage(reason, /*isError=*/true);
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

    if (appType == QLatin1String("host")) {
        m_shell->showPage(PageType::Host);
        if (m_hostPage) {
            m_hostPage->setRoomInfo(m_pendingConfig.roomId,
                m_pendingConfig.password,
                m_pendingConfig.serverUrl);
        }
        m_controlHandler->setActiveRoom(m_pendingConfig.roomId,
            /*deviceControlEnabled=*/false);

        // If sharing was active before a reconnect, restart capture
        // automatically so the stream resumes without user interaction.
        if (m_sharingActive) {
            startCapture();
            m_signaling->emitEvent(QStringLiteral("stream-ready"),
                nlohmann::json::object());
        }
    }
    else {
        m_shell->showPage(PageType::Viewer);
        m_decoder->stop();

        // Show waiting overlay immediately; it will be hidden on the first
        // decoded frame.  If a host is already in the room its peer-joined
        // event will have arrived in the ack peers list and onPeerJoined
        // will update the message – otherwise this default is correct.
        if (m_viewerPage) {
            m_viewerPage->showWaitingOverlay(
                QStringLiteral("Waiting for host to start sharing…"));
        }
    }
}

void AppController::onPeerJoined(const QString& peerId, const QString& appType)
{
    // Update AppState
    PeerInfo statePeer;
    statePeer.id = peerId;
    statePeer.appType = appType;
    statePeer.joinedAt = QDateTime::currentDateTime();
    APP_STATE->addPeer(statePeer);

    // Update ControlEventHandler
    m_controlHandler->addPeer(peerId);

    // "controller" peers get control automatically; viewers only get it when
    // the host explicitly enables it via the Allow Control button.
    if (appType == QLatin1String("controller") && m_controlAllowedByHost) {
        m_controlHandler->setEnabled(true);
        m_controlHandler->setActiveRoom(m_pendingConfig.roomId, true);
    }

    // Track the host peer so we can react when it leaves
    if (appType == QLatin1String("host")) {
        m_hostPeerId = peerId;
        // Host just joined while we're a viewer/controller – clear the overlay
        if (m_viewerPage && m_pendingConfig.appType != QLatin1String("host")) {
            m_viewerPage->showWaitingOverlay(
                QStringLiteral("Host connected – waiting for stream…"));
        }
    }

    // Update HostPage
    if (m_hostPage) {
        m_hostPage->addPeer(statePeer);
        m_hostPage->setStreamStatus(m_capturer->isRunning(),
            APP_STATE->roomInfo().peers.size());
    }

    if (m_pendingConfig.appType == QLatin1String("host") && m_producer->isRunning()) {
        m_producer->forceKeyframe();
    }

    if (m_signaling->isConnected()) {
        m_signaling->emitEvent(QStringLiteral("peer-ack"), {
            { "peerId", peerId.toStdString() }
            });
    }
}

void AppController::onPeerLeft(const QString& peerId)
{
    APP_STATE->removePeer(peerId);
    m_controlHandler->removePeer(peerId);

    // If the host disconnected while we're viewing, stop the decoder and
    // show a clear status message instead of a frozen last frame.
    if (peerId == m_hostPeerId &&
        m_pendingConfig.appType != QLatin1String("host"))
    {
        m_hostPeerId.clear();
        m_decoder->stop();
        if (m_viewerPage) {
            m_viewerPage->showWaitingOverlay(
                QStringLiteral("Host disconnected.\nWaiting for a new host to join…"));
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
    }
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

// ===========================================================================
// Slots – ScreenCapturer
// ===========================================================================

void AppController::onCaptureError(const QString& msg)
{
    Q_UNUSED(msg)
}

// ===========================================================================
// Slots – VideoProducer
// ===========================================================================

void AppController::onVideoStatsUpdated(double fps, int bitrateKbps, int w, int h)
{
    VideoStats s;
    s.fps = static_cast<int>(fps);
    s.bitrate = bitrateKbps;
    s.width = w;
    s.height = h;
    s.latency = 0.0;
    APP_STATE->setVideoStats(s);

    if (m_viewerPage) {
        ConnectionInfo info;
        info.fps = fps;
        info.bitrateKbps = bitrateKbps;
        info.width = w;
        info.height = h;
        info.latencyMs = static_cast<int>(APP_STATE->videoStats().latency);
        m_viewerPage->setConnectionInfo(info);
    }
}

void AppController::onEncodingError(const QString& /*msg*/)
{
}

// ===========================================================================
// Slots – ViewerPage
// ===========================================================================

void AppController::onViewerMouseEvent(float x, float y, float dx, float dy,
    const QString& type,
    const QString& button,
    const QStringList& mods)
{
    if (!m_signaling->isConnected()) { return; }

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
    m_signaling->emitEvent(QStringLiteral("control"), payload);
}

void AppController::onViewerKeyboardEvent(const QString& key,
    const QString& type,
    const QStringList& mods)
{
    if (!m_signaling->isConnected()) { return; }

    nlohmann::json modArr = nlohmann::json::array();
    for (const QString& m : mods) { modArr.push_back(m.toStdString()); }

    nlohmann::json payload = {
        { "type",      "keyboard" },
        { "key",       key.toStdString() },
        { "eventType", type.toStdString() },
        { "modifiers", modArr },
        { "senderId",  m_roomManager->localPeerId().toStdString() }
    };
    m_signaling->emitEvent(QStringLiteral("control"), payload);
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
        // Notify all peers that the stream is now live.
        if (m_signaling->isConnected()) {
            m_signaling->emitEvent(QStringLiteral("stream-ready"),
                nlohmann::json::object());
        }
    }
    else {
        stopCapture();
        // Notify all peers that the stream has stopped.
        if (m_signaling->isConnected()) {
            m_signaling->emitEvent(QStringLiteral("stream-stopped"),
                nlohmann::json::object());
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
    if (!m_producer->isRunning()) {
        // Start the encoder with a nominal 1920×1080 size; VideoProducer::onFrame
       // will automatically restart the encoder at the real capture resolution
             // on the first frame if the DXGI output differs.
        m_producer->start(1920, 1080, s.targetFps, s.bitrateKbps);
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

    // If we're the host and still sharing, tell peers before disconnecting
    // so they get the stream-stopped overlay rather than a frozen frame.
    if (m_pendingConfig.appType == QLatin1String("host") &&
        m_signaling->isConnected())
    {
        m_signaling->emitEvent(QStringLiteral("stream-stopped"),
            nlohmann::json::object());
    }

    stopCapture();
    m_decoder->stop();
    m_roomManager->leaveRoom();
    m_signaling->disconnect();
    m_controlHandler->setEnabled(false);

    APP_STATE->setConnectionState(AppState::ConnectionState::Disconnected);
    APP_STATE->setRoomInfo({});
    m_shell->setConnectionStatus(AppShell::ConnectionStatus::Disconnected);
}
#pragma once

#include <QObject>
#include "AppState.h"   // canonical struct definitions

// Forward declarations
class SignalingClient;
class RoomManager;
class P2PClient;
class ScreenCapturer;
class VideoProducer;
class VideoDecoder;
class ControlEventHandler;
class InputInjector;
class AppShell;
class ConnectModal;
class ViewerPage;
class HostPage;

class AppController : public QObject
{
    Q_OBJECT

public:
    explicit AppController(AppShell* shell, QObject* parent = nullptr);
    ~AppController() override;

    AppController(const AppController&) = delete;
    AppController& operator=(const AppController&) = delete;

    void start();

private slots:
    // ── ConnectModal ──────────────────────────────────────────────────────
    void onConnectRequested(const ConnectionConfig& cfg);

    // ── SignalingClient ───────────────────────────────────────────────────
    void onSignalingConnected();
    void onSignalingDisconnected();
    void onSignalingAuthError(const QString& reason);

    // ── RoomManager ───────────────────────────────────────────────────────
    void onRoomJoined();
    void onPeerJoined(const QString& peerId, const QString& appType);
    void onPeerLeft(const QString& peerId);
    void onStreamReady();
    void onConnectionFailed(const QString& reason);

    // ── ScreenCapturer ────────────────────────────────────────────────────
    void onCaptureError(const QString& msg);

    // ── VideoProducer ─────────────────────────────────────────────────────
    void onVideoStatsUpdated(double fps, int bitrateKbps, int w, int h);
    void onEncodingError(const QString& msg);

    // ── ViewerPage ────────────────────────────────────────────────────────
    void onViewerMouseEvent(float x, float y, float dx, float dy,
        const QString& type, const QString& button,
        const QStringList& mods);
    void onViewerKeyboardEvent(const QString& key, const QString& type,
        const QStringList& mods);
    void onViewerDisconnectRequested();

    // ── HostPage ──────────────────────────────────────────────────────────
    void onShareToggled(bool active);
    void onControlToggled(bool allowed);
    void onKickPeer(const QString& peerId);
    void onSessionEnded();

    // ── AppShell ──────────────────────────────────────────────────────────
    void onSettingsRequested();
    void onDisconnectRequested();

private:
    void wireSignalingClient();
    void wireRoomManager();
    void wireP2PClient();
    void wireScreenCapturer();
    void wireVideoProducer();
    void wireVideoDecoder();
    void wireViewerPage();
    void wireHostPage();
    void wireAppShell();

    void teardownSession();
    void startCapture();
    void stopCapture();

    // ── Owned components ──────────────────────────────────────────────────
    AppShell* m_shell{ nullptr };       // non-owning (created by caller)
    SignalingClient* m_signaling{ nullptr };
    RoomManager* m_roomManager{ nullptr };
    P2PClient* m_p2p{ nullptr };
    ScreenCapturer* m_capturer{ nullptr };
    VideoProducer* m_producer{ nullptr };
    VideoDecoder* m_decoder{ nullptr };
    ControlEventHandler* m_controlHandler{ nullptr };
    InputInjector* m_injector{ nullptr };

    // ── UI pages (non-owning, owned by AppShell) ──────────────────────────
    ConnectModal* m_connectModal{ nullptr };
    ViewerPage* m_viewerPage{ nullptr };
    HostPage* m_hostPage{ nullptr };

    // ── Cached session info ───────────────────────────────────────────────
    ConnectionConfig m_pendingConfig;
    bool             m_sharingActive{ false };
    bool             m_controlAllowedByHost{ false };
    QString          m_hostPeerId;
};

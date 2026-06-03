#pragma once

#include <QObject>
#include "AppState.h"   // canonical struct definitions

// Forward declarations – keeps conflicting component headers out of this TU
class SignalingClient;
class RoomManager;
class MediasoupClient;
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
    /// Takes ownership of the shell; creates all other components internally.
    explicit AppController(AppShell* shell, QObject* parent = nullptr);
    ~AppController() override;

    AppController(const AppController&) = delete;
    AppController& operator=(const AppController&) = delete;

    /// Show the connect dialog and prepare for a new session.
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
    MediasoupClient* m_mediasoup{ nullptr };
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
    bool             m_sharingActive{ false };      ///< True while host share is on
    bool             m_controlAllowedByHost{ false }; ///< True after host clicks Allow Control
    QString          m_hostPeerId;   ///< peer id of the host in the current room
};
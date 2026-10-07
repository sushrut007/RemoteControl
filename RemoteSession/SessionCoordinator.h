#pragma once

#include <QObject>

#include <cstdint>

class DarpanMainWindow;

namespace darpan::signaling {
class QtSignalingClient;
class SignalingController;
} // namespace darpan::signaling

namespace darpan::webrtc {
class WebRtcSession;
} // namespace darpan::webrtc

namespace darpan::platform {
class ScreenCapturer;
} // namespace darpan::platform

namespace darpan {

class SessionCoordinator : public QObject
{
    Q_OBJECT

public:
    explicit SessionCoordinator(DarpanMainWindow *window, QObject *parent = nullptr);

    void connectSignaling();
    void hostCreateRoom(const QString &pin);
    void joinRoom(const QString &roomCode, const QString &pin);
    void leaveRoom();
    void reconnectSignaling();
    void reconnectPeerVideo();
    void requestRemoteControl();
    void grantControl(const QString &deviceId);
    void revokeControl(const QString &deviceId);
    void revokeActiveController();
    void sendFile(const QString &path);
    void cancelFileTransfer(uint32_t transferId);
    void shutdownOnExit();
    void toggleHostScreenSharing();

private:
    void wireSignaling();
    void wireUi();
    void updateLocalControlUi();
    void enterRoomUi(const QString &roomId, bool isHost);
    void updateConnectionStatus(const QString &text);
    void refreshMemberList();
    void maybeStartHostCapture();
    void applyHostSharingActiveState();
    void handleHostDeparted();

    DarpanMainWindow *m_window = nullptr;
    signaling::QtSignalingClient *m_client = nullptr;
    signaling::SignalingController *m_controller = nullptr;
    webrtc::WebRtcSession *m_rtc = nullptr;
    platform::ScreenCapturer *m_capturer = nullptr;

    bool m_signalingReady = false;
    bool m_peerConnected = false;
    bool m_pendingPeerReconnect = false;
    QString m_lastRemotePeerId;
    bool m_pendingHostCreate = false;
    QString m_pendingHostPin;
    QString m_pendingJoinCode;
    QString m_pendingJoinPin;
    QString m_pendingControlDeviceId;
    QString m_pendingControlDisplayName;
    bool m_lastRoomHadPin = false;
    bool m_controlRequestPending = false;
    bool m_hostScreenSharingActive = true;
    bool m_hostDepartedPending = false;
    QString m_lastKnownLocalRole;
    QString m_lastJoinAttemptRoomId;
};

} // namespace darpan

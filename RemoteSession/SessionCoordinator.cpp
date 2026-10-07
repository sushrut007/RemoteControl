#include "SessionCoordinator.h"

#include "RemoteSession/ControlProtocol.h"
#include "RemoteSession/FileTransferManager.h"
#include "RemoteSession/WebRtcSession.h"
#include "Signaling/SignalingController.h"
#include "Signaling/QtSignalingClient.h"
#include "Platform/Windows/ScreenCapturer.h"
#include "Utils/AppSettings.h"
#include "Ui/VideoDisplayWidget.h"
#include "Ui/FileTransferPanel.h"
#include "darpan.h"

#include <QFileDialog>
#include <QTimer>

namespace darpan {

SessionCoordinator::SessionCoordinator(DarpanMainWindow *window, QObject *parent)
    : QObject(parent)
    , m_window(window)
    , m_client(new signaling::QtSignalingClient(this))
    , m_controller(new signaling::SignalingController(this))
    , m_rtc(new webrtc::WebRtcSession(this))
    , m_capturer(new platform::ScreenCapturer(this))
{
    wireSignaling();
    wireUi();
}

void SessionCoordinator::wireSignaling()
{
    m_client->setMessageHandler([this](const QString &json) { m_controller->handleRawMessage(json); });

    connect(m_client, &signaling::QtSignalingClient::connected, this, [this]() {
        updateConnectionStatus(tr("Signaling connected — registering device…"));
        m_client->sendHello(AppSettings::instance().deviceName());
    });

    connect(m_client, &signaling::QtSignalingClient::disconnected, this, [this]() {
        m_signalingReady = false;
        m_pendingPeerReconnect = true;
        updateConnectionStatus(tr("Signaling disconnected. Reconnect signaling, then confirm peer video reconnect."));
        m_window->setPeerReconnectVisible(true);
        m_capturer->stop();
    });

    connect(m_client, &signaling::QtSignalingClient::connectionError, this,
            [this](const QString &message) { updateConnectionStatus(message); });

    connect(m_controller, &signaling::SignalingController::deviceRegistered, this, [this](const QString &) {
        m_signalingReady = true;
        updateConnectionStatus(tr("Ready on signaling server"));
        if (m_pendingHostCreate) {
            m_client->sendCreateRoom(m_pendingHostPin);
            m_pendingHostCreate = false;
            m_pendingHostPin.clear();
        } else if (!m_pendingJoinCode.isEmpty()) {
            m_client->sendJoinRoom(m_pendingJoinCode, m_pendingJoinPin, QStringLiteral("controller"));
            m_pendingJoinCode.clear();
            m_pendingJoinPin.clear();
        }
    });

    connect(m_controller, &signaling::SignalingController::roomCreated, this,
            [this](const QString &roomId, const QString &role) {
                m_lastJoinAttemptRoomId.clear();
                enterRoomUi(roomId, role == QStringLiteral("host"));
            });

    connect(m_controller, &signaling::SignalingController::roomJoined, this,
            [this](const QString &roomId, const QString &role) {
                m_lastJoinAttemptRoomId.clear();
                enterRoomUi(roomId, role == QStringLiteral("host"));
            });

    connect(m_controller, &signaling::SignalingController::roomLeft, this, [this]() {
        m_rtc->stop();
        m_capturer->stop();
        m_peerConnected = false;
        m_window->showHomePages();
    });

    connect(m_controller, &signaling::SignalingController::membersUpdated, this, [this]() {
        refreshMemberList();
        const QString role = m_controller->localRole();
        if (!m_lastKnownLocalRole.isEmpty() && m_lastKnownLocalRole != role) {
            if (role == QStringLiteral("host")) {
                m_window->setRoomHostUi(true);
                m_hostScreenSharingActive = true;
                m_window->updateHostScreenShareButton(true);
                updateConnectionStatus(tr("You are now the host of this room."));
                maybeStartHostCapture();
            } else {
                m_window->setRoomHostUi(false);
            }
            updateLocalControlUi();
        }
        if (m_hostDepartedPending) {
            m_hostDepartedPending = false;
            if (role != QStringLiteral("host")) {
                handleHostDeparted();
            }
        }
        m_lastKnownLocalRole = role;
    });

    connect(m_controller, &signaling::SignalingController::memberLeft, this,
            [this](const QString &, bool wasHost) {
                if (wasHost && m_controller->localRole() != QStringLiteral("host")) {
                    m_hostDepartedPending = true;
                }
            });

    connect(m_controller, &signaling::SignalingController::screenShareStateChanged, this,
            [this](const QString &hostDeviceId, bool active) {
                if (m_controller->localRole() == QStringLiteral("host")) {
                    return;
                }
                if (hostDeviceId != m_controller->hostDeviceId() && !m_controller->hostDeviceId().isEmpty()) {
                    return;
                }
                if (active) {
                    m_window->videoWidget()->setPreviewMode(VideoDisplayWidget::PreviewMode::RemotePeer);
                    m_window->setSessionSharingIndicator(false, {});
                    updateConnectionStatus(tr("Host resumed screen sharing"));
                } else {
                    m_window->videoWidget()->clearFrame();
                    m_window->videoWidget()->setPreviewMode(VideoDisplayWidget::PreviewMode::SharingPaused);
                    m_window->videoWidget()->showPlaceholder();
                    m_window->setSessionSharingIndicator(
                        true, tr("Host paused screen sharing — waiting for the host to resume"));
                    updateConnectionStatus(tr("Host paused screen sharing"));
                }
            });

    connect(m_controller, &signaling::SignalingController::memberJoined, this, [this](const RoomMemberInfo &member) {
        if (m_controller->localRole() != QStringLiteral("host")) {
            return;
        }
        if (member.deviceId == m_controller->deviceId()) {
            return;
        }
        if (m_rtc->isActive()) {
            return;
        }
        m_lastRemotePeerId = member.deviceId;
        m_rtc->setSignalingPeerId(member.deviceId);
        m_rtc->configureIce(AppSettings::instance().stunServer(), AppSettings::instance().turnServer(),
                            AppSettings::instance().turnUsername(), AppSettings::instance().turnPassword());
        m_rtc->startAsHost();
        QTimer::singleShot(0, this, [this]() { maybeStartHostCapture(); });
    });

    connect(m_controller, &signaling::SignalingController::peerSignalOffer, this,
            [this](const QString &from, const QString &sdp) {
                // Host always creates the offer when a peer joins; ignore glare offers from joiners.
                if (m_controller->localRole() == QStringLiteral("host")) {
                    return;
                }
                m_lastRemotePeerId = from;
                m_rtc->configureIce(AppSettings::instance().stunServer(), AppSettings::instance().turnServer(),
                                    AppSettings::instance().turnUsername(), AppSettings::instance().turnPassword());
                m_rtc->handleRemoteOffer(from, sdp);
            });

    connect(m_controller, &signaling::SignalingController::peerSignalAnswer, this,
            [this](const QString &from, const QString &sdp) {
                if (m_controller->localRole() != QStringLiteral("host")) {
                    return;
                }
                m_rtc->handleRemoteAnswer(from, sdp);
            });

    connect(m_controller, &signaling::SignalingController::peerSignalIce, this,
            [this](const QString &from, const QJsonObject &candidate) {
                m_rtc->handleRemoteIce(from, candidate);
            });

    connect(m_controller, &signaling::SignalingController::controlStateChanged, this,
            [this](const QString &deviceId, const QString &state) {
                if (deviceId == m_controller->deviceId()) {
                    if (state == QStringLiteral("control_granted")) {
                        m_controlRequestPending = false;
                    } else if (state == QStringLiteral("view_only")) {
                        m_controlRequestPending = false;
                    }
                }
                refreshMemberList();
                updateLocalControlUi();
            });

    connect(m_controller, &signaling::SignalingController::controlRequested, this,
            [this](const QString &deviceId, const QString &displayName) {
                m_pendingControlDeviceId = deviceId;
                m_pendingControlDisplayName = displayName;
                if (m_window->confirmGrantControl(displayName)) {
                    grantControl(deviceId);
                } else {
                    m_pendingControlDeviceId.clear();
                }
            });

    connect(m_controller, &signaling::SignalingController::signalingError, this,
            [this](const QString &code, const QString &message) {
                if (code == QStringLiteral("ROOM_FULL")) {
                    updateConnectionStatus(
                        tr("Room is full — only one host and one controller can be in a session."));
                    return;
                }
                if (code == QStringLiteral("ROOM_NOT_FOUND")) {
                    if (!m_lastJoinAttemptRoomId.isEmpty()) {
                        AppSettings::instance().removeRecentRoom(m_lastJoinAttemptRoomId);
                        m_window->refreshRecentRoomCards();
                    }
                    m_lastJoinAttemptRoomId.clear();
                    updateConnectionStatus(
                        tr("Room not found (server may have restarted) — removed from recent rooms."));
                    return;
                }
                updateConnectionStatus(QStringLiteral("%1: %2").arg(code, message));
            });

    connect(m_rtc, &webrtc::WebRtcSession::localOfferCreated, this, [this](const QString &sdp) {
        m_client->sendSignalOffer(m_lastRemotePeerId, sdp);
    });

    connect(m_rtc, &webrtc::WebRtcSession::localAnswerCreated, this,
            [this](const QString &sdp, const QString &target) { m_client->sendSignalAnswer(target, sdp); });

    connect(m_rtc, &webrtc::WebRtcSession::localIceCandidate, this,
            [this](const QString &target, const QString &candidateJson) {
                m_client->sendSignalIce(target, candidateJson);
            });

    connect(m_rtc, &webrtc::WebRtcSession::uiStateChanged, this, [this](webrtc::PeerConnectionUiState state) {
        if (state == webrtc::PeerConnectionUiState::Connected) {
            m_peerConnected = true;
            m_pendingPeerReconnect = false;
            updateConnectionStatus(tr("Peer connected (view-only until host grants control)"));
            if (m_controller->localRole() == QStringLiteral("host")) {
                applyHostSharingActiveState();
                maybeStartHostCapture();
            } else {
                m_window->setSessionSharingIndicator(true, tr("Remote session active"));
            }
        } else if (state == webrtc::PeerConnectionUiState::Disconnected) {
            m_peerConnected = false;
            if (m_controller->localRole() == QStringLiteral("host")) {
                m_capturer->stop();
            }
            m_window->setSessionSharingIndicator(false, {});
            m_pendingPeerReconnect = true;
            m_window->setPeerReconnectVisible(true);
            updateConnectionStatus(tr("Peer disconnected — use “Reconnect video” after signaling is stable"));
        }
    });

    connect(m_rtc, &webrtc::WebRtcSession::remoteFrameReady, this,
            [this](const QImage &frame) { m_window->setRemoteVideoFrame(frame); });

    connect(m_rtc, &webrtc::WebRtcSession::remoteStreamResized, this, [this](const QSize &size) {
        m_window->videoWidget()->setStreamSize(size);
    });

    connect(m_rtc, &webrtc::WebRtcSession::previewChannelOpen, this, [this]() { maybeStartHostCapture(); });

    connect(m_capturer, &platform::ScreenCapturer::frameCaptured, this,
            [this](const QImage &frame, const QSize &streamSize) {
                if (!m_hostScreenSharingActive) {
                    return;
                }
                m_window->setCaptureBackendText(m_capturer->captureBackend());
                if (m_controller->localRole() == QStringLiteral("host")) {
                    m_window->videoWidget()->setStreamSize(streamSize);
                    m_window->setRemoteVideoFrame(frame);
                }
                m_rtc->pushHostVideoFrame(frame, streamSize);
            });
}

void SessionCoordinator::connectSignaling()
{
    m_client->connectToServer(AppSettings::instance().signalingUrl());
}

void SessionCoordinator::hostCreateRoom(const QString &pin)
{
    m_lastRoomHadPin = !pin.isEmpty();
    if (!m_signalingReady) {
        m_pendingHostCreate = true;
        m_pendingHostPin = pin;
        connectSignaling();
        return;
    }
    m_client->sendCreateRoom(pin);
}

void SessionCoordinator::joinRoom(const QString &roomCode, const QString &pin)
{
    m_lastRoomHadPin = !pin.isEmpty();
    m_lastJoinAttemptRoomId = roomCode.trimmed().toUpper();
    if (!m_signalingReady) {
        m_pendingJoinCode = roomCode;
        m_pendingJoinPin = pin;
        connectSignaling();
        return;
    }
    m_client->sendJoinRoom(roomCode, pin, QStringLiteral("controller"));
}

void SessionCoordinator::leaveRoom()
{
    m_rtc->stop();
    m_capturer->stop();
    m_hostScreenSharingActive = true;
    m_hostDepartedPending = false;
    m_client->sendLeaveRoom();
    m_window->setSessionSharingIndicator(false, {});
    m_window->showHomePages();
}

void SessionCoordinator::shutdownOnExit()
{
    m_rtc->stop();
    m_capturer->stop();
    if (m_controller->inRoom()) {
        m_client->sendLeaveRoom();
    }
    m_client->shutdownOnExit();
}

void SessionCoordinator::reconnectSignaling()
{
    m_client->reconnectSignalingOnly();
}

void SessionCoordinator::reconnectPeerVideo()
{
    if (!m_pendingPeerReconnect && m_peerConnected) {
        return;
    }
    m_rtc->stop();
    m_capturer->stop();
    m_peerConnected = false;
    m_window->setPeerReconnectVisible(false);
    if (m_controller->localRole() == QStringLiteral("host") && !m_lastRemotePeerId.isEmpty()) {
        m_rtc->setSignalingPeerId(m_lastRemotePeerId);
        m_rtc->startAsHost();
        maybeStartHostCapture();
    } else {
        updateConnectionStatus(tr("Waiting for host to re-initiate WebRTC offer…"));
    }
}

void SessionCoordinator::enterRoomUi(const QString &roomId, bool isHost)
{
    m_controlRequestPending = false;
    m_hostDepartedPending = false;
    m_lastKnownLocalRole = isHost ? QStringLiteral("host") : QStringLiteral("controller");
    if (isHost) {
        m_hostScreenSharingActive = true;
    }
    m_window->showRoomPage(roomId, isHost);
    m_window->recordRecentRoom(roomId, m_lastRoomHadPin);
    refreshMemberList();
    updateLocalControlUi();
    if (isHost) {
        maybeStartHostCapture();
        applyHostSharingActiveState();
        updateConnectionStatus(tr("Room open — local preview below; waiting for a peer to connect"));
    } else {
        updateConnectionStatus(tr("Joined room — negotiating…"));
    }
}

void SessionCoordinator::updateConnectionStatus(const QString &text)
{
    m_window->setStatusText(text);
}

void SessionCoordinator::refreshMemberList()
{
    m_window->setRoomMembers(m_controller->members(), m_controller->deviceId());
}

void SessionCoordinator::wireUi()
{
    auto *video = m_window->videoWidget();
    connect(video, &VideoDisplayWidget::mouseMoveNormalized, this, [this](uint16_t nx, uint16_t ny) {
        m_rtc->sendControlPayload(protocol::packMouseMove(nx, ny));
    });
    connect(video, &VideoDisplayWidget::mouseButtonNormalized, this,
            [this](uint8_t button, bool down, uint16_t nx, uint16_t ny) {
                m_rtc->sendControlPayload(protocol::packMouseButton(button, down, nx, ny));
            });
    connect(video, &VideoDisplayWidget::wheelNormalized, this, [this](int16_t delta, uint16_t nx, uint16_t ny) {
        m_rtc->sendControlPayload(protocol::packMouseWheel(delta, nx, ny));
    });
    connect(video, &VideoDisplayWidget::keyEvent, this, [this](uint16_t vk, bool down, bool extended) {
        m_rtc->sendControlPayload(protocol::packKey(down, vk, extended));
    });

    auto *files = m_rtc->fileTransfer();
    auto *panel = m_window->fileTransferPanel();
    connect(files, &webrtc::FileTransferManager::incomingOffer, this,
            [this, files, panel](uint32_t id, const QString &name, quint64 size, const QByteArray &) {
                const QString savePath = QFileDialog::getSaveFileName(m_window, tr("Save incoming file"), name);
                if (savePath.isEmpty()) {
                    files->rejectIncoming(id);
                    panel->finishTransfer(id, false, tr("Declined"));
                } else {
                    files->acceptIncoming(id, savePath);
                }
            });
    connect(files, &webrtc::FileTransferManager::transferStarted, panel,
            &FileTransferPanel::addTransfer);
    connect(files, &webrtc::FileTransferManager::transferProgress, panel,
            &FileTransferPanel::updateProgress);
    connect(files, &webrtc::FileTransferManager::transferFinished, panel,
            &FileTransferPanel::finishTransfer);
}

void SessionCoordinator::updateLocalControlUi()
{
    const QString selfId = m_controller->deviceId();
    const bool isHost = m_controller->localRole() == QStringLiteral("host");
    bool selfGranted = false;
    bool peerHasControl = false;
    for (const RoomMemberInfo &m : m_controller->members()) {
        if (m.controlState != QStringLiteral("control_granted")) {
            continue;
        }
        if (m.deviceId == selfId) {
            selfGranted = true;
        } else if (m.role != QStringLiteral("host")) {
            peerHasControl = true;
        }
    }

    m_window->updateControlButtons(isHost, selfGranted, peerHasControl, m_controlRequestPending);
    m_window->videoWidget()->setRemoteControlEnabled(selfGranted);

    if (isHost) {
        m_rtc->setHostControlInjectionEnabled(peerHasControl);
    }
}

void SessionCoordinator::requestRemoteControl()
{
    if (m_controller->localRole() == QStringLiteral("host")) {
        return;
    }
    m_controlRequestPending = true;
    m_client->sendRequestControl();
    updateConnectionStatus(tr("Control request sent to host"));
    updateLocalControlUi();
}

void SessionCoordinator::grantControl(const QString &deviceId)
{
    m_client->sendGrantControl(deviceId);
    m_pendingControlDeviceId.clear();
}

void SessionCoordinator::revokeControl(const QString &deviceId)
{
    m_client->sendRevokeControl(deviceId);
    m_rtc->setHostControlInjectionEnabled(false);
}

void SessionCoordinator::revokeActiveController()
{
    if (!m_lastRemotePeerId.isEmpty()) {
        revokeControl(m_lastRemotePeerId);
    }
}

void SessionCoordinator::sendFile(const QString &path)
{
    m_rtc->fileTransfer()->sendFile(path);
}

void SessionCoordinator::cancelFileTransfer(uint32_t transferId)
{
    m_rtc->fileTransfer()->cancelTransfer(transferId);
    if (auto *panel = m_window->fileTransferPanel()) {
        panel->finishTransfer(transferId, false, tr("Cancelled"));
    }
}

void SessionCoordinator::maybeStartHostCapture()
{
    if (m_controller->localRole() != QStringLiteral("host")) {
        return;
    }
    if (!m_hostScreenSharingActive) {
        return;
    }
    if (!m_capturer->isActive()) {
        m_capturer->start(100);
    }
}

void SessionCoordinator::applyHostSharingActiveState()
{
    if (m_controller->localRole() != QStringLiteral("host")) {
        return;
    }
    m_window->updateHostScreenShareButton(m_hostScreenSharingActive);
    if (m_hostScreenSharingActive) {
        m_window->setSessionSharingIndicator(
            true, tr("Screen sharing active — connected peers can see your display"));
    } else {
        m_window->setSessionSharingIndicator(
            true, tr("Screen sharing paused — controllers cannot see your display"));
    }
}

void SessionCoordinator::toggleHostScreenSharing()
{
    if (m_controller->localRole() != QStringLiteral("host")) {
        return;
    }
    m_hostScreenSharingActive = !m_hostScreenSharingActive;
    if (m_hostScreenSharingActive) {
        maybeStartHostCapture();
        m_client->sendSetScreenShare(true);
        m_window->videoWidget()->setPreviewMode(VideoDisplayWidget::PreviewMode::HostLocal);
        updateConnectionStatus(tr("Screen sharing started"));
    } else {
        m_capturer->stop();
        m_window->videoWidget()->clearFrame();
        m_window->videoWidget()->setPreviewMode(VideoDisplayWidget::PreviewMode::HostSharingPaused);
        m_window->videoWidget()->showPlaceholder();
        m_client->sendSetScreenShare(false);
        updateConnectionStatus(tr("Screen sharing stopped"));
    }
    applyHostSharingActiveState();
}

void SessionCoordinator::handleHostDeparted()
{
    if (!m_hostDepartedPending) {
        return;
    }
    m_hostDepartedPending = false;
    if (m_controller->localRole() == QStringLiteral("host")) {
        return;
    }
    m_window->videoWidget()->clearFrame();
    m_window->videoWidget()->setPreviewMode(VideoDisplayWidget::PreviewMode::HostDeparted);
    m_window->videoWidget()->showPlaceholder();
    m_window->setSessionSharingIndicator(
        true,
        tr("Host left the room. Leave this session and rejoin the same room code to become the new host."));
    updateConnectionStatus(
        tr("Host left — leave the room and rejoin with the same code to become host (room stays open)."));
}

} // namespace darpan

#pragma once

#include <QByteArray>
#include <QImage>
#include <QJsonObject>
#include <QObject>
#include <QSize>
#include <QString>

namespace darpan::webrtc {

enum class SessionRole { Host, Client };

enum class PeerConnectionUiState {
    Idle,
    Negotiating,
    Connected,
    Disconnected,
};

class FileTransferManager;

class WebRtcSession : public QObject
{
    Q_OBJECT

public:
    explicit WebRtcSession(QObject *parent = nullptr);
    ~WebRtcSession() override;

    void configureIce(const QString &stunUrl, const QString &turnUrl, const QString &turnUser,
                      const QString &turnPassword);

    void startAsHost();
    void startAsClient();
    void stop();

    void setSignalingPeerId(const QString &deviceId);

    void handleRemoteOffer(const QString &fromDeviceId, const QString &sdp);
    void handleRemoteAnswer(const QString &fromDeviceId, const QString &sdp);
    void handleRemoteIce(const QString &fromDeviceId, const QJsonObject &candidate);

    void pushHostVideoFrame(const QImage &frame, const QSize &streamSize);

    void setHostControlInjectionEnabled(bool enabled);
    void sendControlPayload(const QByteArray &payload);

    FileTransferManager *fileTransfer() const;

    PeerConnectionUiState uiState() const { return m_uiState; }
    bool isActive() const { return m_active; }
    QSize lastStreamSize() const { return m_lastStreamSize; }

signals:
    void uiStateChanged(PeerConnectionUiState state);
    void localOfferCreated(const QString &sdp);
    void localAnswerCreated(const QString &sdp, const QString &targetDeviceId);
    void localIceCandidate(const QString &targetDeviceId, const QString &candidateJson);
    void remoteFrameReady(const QImage &frame);
    void remoteStreamResized(const QSize &size);
    void previewChannelOpen();
    void errorOccurred(const QString &message);

private:
    void setUiState(PeerConnectionUiState state);
    void setupPeerConnection(SessionRole role);
    void sendPreviewFrame(const QImage &frame, const QSize &streamSize);
    void onPreviewChannelReady();
    void handleControlBinary(const QByteArray &payload);
    void deliverRemotePreview(QImage image, uint16_t width, uint16_t height);

    SessionRole m_role = SessionRole::Client;
    PeerConnectionUiState m_uiState = PeerConnectionUiState::Idle;
    bool m_active = false;
    bool m_hostControlEnabled = false;
    QString m_remoteDeviceId;
    QString m_stunUrl;
    QString m_turnUrl;
    QString m_turnUser;
    QString m_turnPassword;
    QSize m_lastStreamSize = QSize(1920, 1080);

    struct Impl;
    Impl *m_impl = nullptr;
};

} // namespace darpan::webrtc

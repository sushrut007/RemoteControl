#pragma once

#include "Signaling/SignalingClient.h"

#include <QObject>
#include <QSslError>
#include <QWebSocket>

namespace darpan::signaling {

class QtSignalingClient final : public QObject, public SignalingClient
{
    Q_OBJECT

public:
    explicit QtSignalingClient(QObject *parent = nullptr);
    ~QtSignalingClient() override;

    void setMessageHandler(MessageHandler handler) override;
    void connectToServer(const QString &webSocketUrl) override;
    void disconnect() override;

    void sendHello(const QString &deviceName) override;
    void sendCreateRoom(const QString &pin = {}) override;
    void sendJoinRoom(const QString &roomId,
                      const QString &pin = {},
                      const QString &roleHint = {}) override;
    void sendLeaveRoom() override;
    void sendSignalOffer(const QString &targetDeviceId, const QString &sdp) override;
    void sendSignalAnswer(const QString &targetDeviceId, const QString &sdp) override;
    void sendSignalIce(const QString &targetDeviceId, const QString &candidateJson) override;
    void sendGrantControl(const QString &targetDeviceId) override;
    void sendRevokeControl(const QString &targetDeviceId) override;
    void sendRequestControl() override;
    void sendSetScreenShare(bool active) override;

    void reconnectSignalingOnly();

    bool isConnected() const;
    void shutdownOnExit();

signals:
    void connected();
    void disconnected();
    void connectionError(const QString &message);

private slots:
    void onTextMessageReceived(const QString &message);
    void onSocketConnected();
    void onSocketDisconnected();
    void onSocketError(QAbstractSocket::SocketError error);
    void onSslErrors(const QList<QSslError> &errors);

private:
    bool verifyCertificatePin(const QSslCertificate &cert) const;
    void sendJson(const QJsonObject &obj);

    QWebSocket m_socket;
    MessageHandler m_handler;
    QString m_pendingUrl;
};

} // namespace darpan::signaling

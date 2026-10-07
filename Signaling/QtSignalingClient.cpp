#include "SignalingClient.h"
#include "QtSignalingClient.h"

#include "Utils/WebSocketUrl.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSslCertificate>
#include <QSslError>
#include <QCryptographicHash>

#include "Utils/AppSettings.h"

namespace darpan::signaling {

namespace {
constexpr int kProtocolVersion = 1;
}

QtSignalingClient::QtSignalingClient(QObject *parent)
    : QObject(parent)
{
    connect(&m_socket, &QWebSocket::textMessageReceived, this, &QtSignalingClient::onTextMessageReceived);
    connect(&m_socket, &QWebSocket::connected, this, &QtSignalingClient::onSocketConnected);
    connect(&m_socket, &QWebSocket::disconnected, this, &QtSignalingClient::onSocketDisconnected);
    connect(&m_socket, &QWebSocket::errorOccurred, this, &QtSignalingClient::onSocketError);
    connect(&m_socket, &QWebSocket::sslErrors, this, &QtSignalingClient::onSslErrors);
}

QtSignalingClient::~QtSignalingClient()
{
    m_socket.close();
}

void QtSignalingClient::setMessageHandler(MessageHandler handler)
{
    m_handler = std::move(handler);
}

void QtSignalingClient::connectToServer(const QString &webSocketUrl)
{
    m_pendingUrl = darpan::util::normalizeSignalingWebSocketUrl(webSocketUrl);
    if (m_socket.state() == QAbstractSocket::ConnectedState) {
        m_socket.close();
    }
    m_socket.open(QUrl(m_pendingUrl));
}

void QtSignalingClient::disconnect()
{
    m_socket.close();
}

void QtSignalingClient::reconnectSignalingOnly()
{
    if (m_pendingUrl.isEmpty()) {
        return;
    }
    connectToServer(m_pendingUrl);
}

void QtSignalingClient::shutdownOnExit()
{
    m_socket.close();
}

bool QtSignalingClient::verifyCertificatePin(const QSslCertificate &cert) const
{
    const AppSettings &settings = AppSettings::instance();
    if (!settings.signalingTlsPinEnabled()) {
        return true;
    }
    const QString expected = settings.signalingTlsPinSha256().toLower();
    if (expected.isEmpty()) {
        return false;
    }
    const QString digest =
        QString::fromLatin1(cert.digest(QCryptographicHash::Sha256).toHex().toLower());
    return digest == expected;
}

void QtSignalingClient::onSslErrors(const QList<QSslError> &errors)
{
    Q_UNUSED(errors);
    const AppSettings &settings = AppSettings::instance();
    if (!settings.signalingTlsPinEnabled()) {
        return;
    }
    const QSslCertificate cert = m_socket.sslConfiguration().peerCertificate();
    if (verifyCertificatePin(cert)) {
        m_socket.ignoreSslErrors();
        return;
    }
    emit connectionError(QStringLiteral("TLS certificate pin mismatch"));
    m_socket.close();
}

bool QtSignalingClient::isConnected() const
{
    return m_socket.state() == QAbstractSocket::ConnectedState;
}

void QtSignalingClient::sendHello(const QString &deviceName)
{
    sendJson({{QStringLiteral("v"), kProtocolVersion},
              {QStringLiteral("type"), QStringLiteral("hello")},
              {QStringLiteral("device_name"), deviceName}});
}

void QtSignalingClient::sendCreateRoom(const QString &pin)
{
    QJsonObject obj{{QStringLiteral("v"), kProtocolVersion},
                    {QStringLiteral("type"), QStringLiteral("create_room")}};
    if (!pin.isEmpty()) {
        obj.insert(QStringLiteral("pin"), pin);
    }
    sendJson(obj);
}

void QtSignalingClient::sendJoinRoom(const QString &roomId, const QString &pin, const QString &roleHint)
{
    QJsonObject obj{{QStringLiteral("v"), kProtocolVersion},
                    {QStringLiteral("type"), QStringLiteral("join_room")},
                    {QStringLiteral("room_id"), roomId.trimmed().toUpper()}};
    if (!pin.isEmpty()) {
        obj.insert(QStringLiteral("pin"), pin);
    }
    if (!roleHint.isEmpty()) {
        obj.insert(QStringLiteral("role_hint"), roleHint);
    }
    sendJson(obj);
}

void QtSignalingClient::sendLeaveRoom()
{
    sendJson({{QStringLiteral("v"), kProtocolVersion}, {QStringLiteral("type"), QStringLiteral("leave_room")}});
}

void QtSignalingClient::sendSignalOffer(const QString &targetDeviceId, const QString &sdp)
{
    sendJson({{QStringLiteral("v"), kProtocolVersion},
              {QStringLiteral("type"), QStringLiteral("signal")},
              {QStringLiteral("signal_type"), QStringLiteral("offer")},
              {QStringLiteral("target_device_id"), targetDeviceId},
              {QStringLiteral("sdp"), sdp}});
}

void QtSignalingClient::sendSignalAnswer(const QString &targetDeviceId, const QString &sdp)
{
    sendJson({{QStringLiteral("v"), kProtocolVersion},
              {QStringLiteral("type"), QStringLiteral("signal")},
              {QStringLiteral("signal_type"), QStringLiteral("answer")},
              {QStringLiteral("target_device_id"), targetDeviceId},
              {QStringLiteral("sdp"), sdp}});
}

void QtSignalingClient::sendSignalIce(const QString &targetDeviceId, const QString &candidateJson)
{
    const QJsonDocument doc = QJsonDocument::fromJson(candidateJson.toUtf8());
    sendJson({{QStringLiteral("v"), kProtocolVersion},
              {QStringLiteral("type"), QStringLiteral("signal")},
              {QStringLiteral("signal_type"), QStringLiteral("ice")},
              {QStringLiteral("target_device_id"), targetDeviceId},
              {QStringLiteral("candidate"), doc.object()}});
}

void QtSignalingClient::sendGrantControl(const QString &targetDeviceId)
{
    sendJson({{QStringLiteral("v"), kProtocolVersion},
              {QStringLiteral("type"), QStringLiteral("grant_control")},
              {QStringLiteral("target_device_id"), targetDeviceId}});
}

void QtSignalingClient::sendRevokeControl(const QString &targetDeviceId)
{
    sendJson({{QStringLiteral("v"), kProtocolVersion},
              {QStringLiteral("type"), QStringLiteral("revoke_control")},
              {QStringLiteral("target_device_id"), targetDeviceId}});
}

void QtSignalingClient::sendRequestControl()
{
    sendJson({{QStringLiteral("v"), kProtocolVersion}, {QStringLiteral("type"), QStringLiteral("request_control")}});
}

void QtSignalingClient::sendSetScreenShare(bool active)
{
    sendJson({{QStringLiteral("v"), kProtocolVersion},
              {QStringLiteral("type"), QStringLiteral("set_screen_share")},
              {QStringLiteral("active"), active}});
}

void QtSignalingClient::sendJson(const QJsonObject &obj)
{
    if (m_socket.state() != QAbstractSocket::ConnectedState) {
        emit connectionError(QStringLiteral("Signaling socket is not connected"));
        return;
    }
    m_socket.sendTextMessage(QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)));
}

void QtSignalingClient::onTextMessageReceived(const QString &message)
{
    if (m_handler) {
        m_handler(message);
    }
}

void QtSignalingClient::onSocketConnected()
{
    emit connected();
}

void QtSignalingClient::onSocketDisconnected()
{
    emit disconnected();
}

void QtSignalingClient::onSocketError(QAbstractSocket::SocketError)
{
    emit connectionError(m_socket.errorString());
}

} // namespace darpan::signaling

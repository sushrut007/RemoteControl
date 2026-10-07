#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVector>

struct RoomMemberInfo
{
    QString deviceId;
    QString displayName;
    QString role;
    QString controlState;
};

namespace darpan::signaling {

class SignalingController : public QObject
{
    Q_OBJECT

public:
    explicit SignalingController(QObject *parent = nullptr);

    void handleRawMessage(const QString &json);

    QString deviceId() const { return m_deviceId; }
    QString roomId() const { return m_roomId; }
    QString localRole() const { return m_localRole; }
    QString hostDeviceId() const { return m_hostDeviceId; }
    QVector<RoomMemberInfo> members() const { return m_members; }
    bool isRegistered() const { return !m_deviceId.isEmpty(); }
    bool inRoom() const { return !m_roomId.isEmpty(); }

signals:
    void deviceRegistered(const QString &deviceId);
    void roomCreated(const QString &roomId, const QString &role);
    void roomJoined(const QString &roomId, const QString &role);
    void roomLeft();
    void membersUpdated(const QVector<RoomMemberInfo> &members);
    void memberJoined(const RoomMemberInfo &member);
    void memberLeft(const QString &deviceId, bool wasHost);
    void roomHostUpdated(const QString &hostDeviceId);
    void screenShareStateChanged(const QString &hostDeviceId, bool active);
    void peerSignalOffer(const QString &fromDeviceId, const QString &sdp);
    void peerSignalAnswer(const QString &fromDeviceId, const QString &sdp);
    void peerSignalIce(const QString &fromDeviceId, const QJsonObject &candidate);
    void controlStateChanged(const QString &deviceId, const QString &state);
    void controlRequested(const QString &deviceId, const QString &displayName);
    void signalingError(const QString &code, const QString &message);

private:
    void parseMembers(const QJsonArray &array);
    RoomMemberInfo parseMember(const QJsonObject &obj) const;
    void syncLocalRoleFromMembers();
    void syncHostFromMembers();

    QString m_deviceId;
    QString m_roomId;
    QString m_localRole;
    QString m_hostDeviceId;
    QVector<RoomMemberInfo> m_members;
};

} // namespace darpan::signaling

#include "SignalingController.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

namespace darpan::signaling {

SignalingController::SignalingController(QObject *parent)
    : QObject(parent)
{
}

void SignalingController::handleRawMessage(const QString &json)
{
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (!doc.isObject()) {
        return;
    }
    const QJsonObject obj = doc.object();
    const QString type = obj.value(QStringLiteral("type")).toString();

    if (type == QStringLiteral("hello_ok")) {
        m_deviceId = obj.value(QStringLiteral("device_id")).toString();
        emit deviceRegistered(m_deviceId);
        return;
    }

    if (type == QStringLiteral("error")) {
        emit signalingError(obj.value(QStringLiteral("code")).toString(),
                            obj.value(QStringLiteral("message")).toString());
        return;
    }

    if (type == QStringLiteral("room_created")) {
        m_roomId = obj.value(QStringLiteral("room_id")).toString();
        m_localRole = obj.value(QStringLiteral("role")).toString();
        parseMembers(obj.value(QStringLiteral("members")).toArray());
        syncHostFromMembers();
        emit roomCreated(m_roomId, m_localRole);
        emit membersUpdated(m_members);
        return;
    }

    if (type == QStringLiteral("room_joined")) {
        m_roomId = obj.value(QStringLiteral("room_id")).toString();
        m_localRole = obj.value(QStringLiteral("role")).toString();
        parseMembers(obj.value(QStringLiteral("members")).toArray());
        syncHostFromMembers();
        emit roomJoined(m_roomId, m_localRole);
        emit membersUpdated(m_members);
        return;
    }

    if (type == QStringLiteral("room_left")) {
        m_roomId.clear();
        m_localRole.clear();
        m_hostDeviceId.clear();
        m_members.clear();
        emit roomLeft();
        emit membersUpdated(m_members);
        return;
    }

    if (type == QStringLiteral("room_state")) {
        const QString hostFromPayload = obj.value(QStringLiteral("host_device_id")).toString();
        parseMembers(obj.value(QStringLiteral("members")).toArray());
        syncLocalRoleFromMembers();
        if (!hostFromPayload.isEmpty()) {
            m_hostDeviceId = hostFromPayload;
        } else {
            syncHostFromMembers();
        }
        emit roomHostUpdated(m_hostDeviceId);
        emit membersUpdated(m_members);
        return;
    }

    if (type == QStringLiteral("member_joined")) {
        const RoomMemberInfo member = parseMember(obj.value(QStringLiteral("member")).toObject());
        m_members.append(member);
        emit memberJoined(member);
        emit membersUpdated(m_members);
        return;
    }

    if (type == QStringLiteral("member_left")) {
        const QString id = obj.value(QStringLiteral("device_id")).toString();
        const bool wasHost = !m_hostDeviceId.isEmpty() && id == m_hostDeviceId;
        m_members.erase(
            std::remove_if(m_members.begin(), m_members.end(),
                           [&id](const RoomMemberInfo &m) { return m.deviceId == id; }),
            m_members.end());
        emit memberLeft(id, wasHost);
        emit membersUpdated(m_members);
        return;
    }

    if (type == QStringLiteral("screen_share_state")) {
        emit screenShareStateChanged(obj.value(QStringLiteral("device_id")).toString(),
                                     obj.value(QStringLiteral("active")).toBool(true));
        return;
    }

    if (type == QStringLiteral("signal")) {
        const QString from = obj.value(QStringLiteral("from_device_id")).toString();
        const QString signalType = obj.value(QStringLiteral("signal_type")).toString();
        if (signalType == QStringLiteral("offer")) {
            emit peerSignalOffer(from, obj.value(QStringLiteral("sdp")).toString());
        } else if (signalType == QStringLiteral("answer")) {
            emit peerSignalAnswer(from, obj.value(QStringLiteral("sdp")).toString());
        } else if (signalType == QStringLiteral("ice")) {
            emit peerSignalIce(from, obj.value(QStringLiteral("candidate")).toObject());
        }
        return;
    }

    if (type == QStringLiteral("control_request")) {
        emit controlRequested(obj.value(QStringLiteral("device_id")).toString(),
                              obj.value(QStringLiteral("display_name")).toString());
        return;
    }

    if (type == QStringLiteral("control_state")) {
        const QString deviceId = obj.value(QStringLiteral("device_id")).toString();
        const QString state = obj.value(QStringLiteral("state")).toString();
        for (RoomMemberInfo &member : m_members) {
            if (member.deviceId == deviceId) {
                member.controlState = state;
            }
        }
        emit controlStateChanged(deviceId, state);
        emit membersUpdated(m_members);
    }
}

void SignalingController::parseMembers(const QJsonArray &array)
{
    m_members.clear();
    for (const QJsonValue &value : array) {
        if (value.isObject()) {
            m_members.append(parseMember(value.toObject()));
        }
    }
}

RoomMemberInfo SignalingController::parseMember(const QJsonObject &obj) const
{
    RoomMemberInfo info;
    info.deviceId = obj.value(QStringLiteral("device_id")).toString();
    info.displayName = obj.value(QStringLiteral("display_name")).toString();
    info.role = obj.value(QStringLiteral("role")).toString();
    info.controlState = obj.value(QStringLiteral("control_state")).toString();
    return info;
}

void SignalingController::syncLocalRoleFromMembers()
{
    for (const RoomMemberInfo &member : m_members) {
        if (member.deviceId == m_deviceId) {
            m_localRole = member.role;
            return;
        }
    }
}

void SignalingController::syncHostFromMembers()
{
    m_hostDeviceId.clear();
    for (const RoomMemberInfo &member : m_members) {
        if (member.role == QStringLiteral("host")) {
            m_hostDeviceId = member.deviceId;
            return;
        }
    }
}

} // namespace darpan::signaling

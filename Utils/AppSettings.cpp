#include "AppSettings.h"

#include "include/AppConstants.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>

#include <algorithm>

namespace {

QString envOrSettings(const char *envKey, const QString &settingsKey, const QString &defaultValue = {})
{
    const QString env = qEnvironmentVariable(envKey);
    if (!env.isEmpty()) {
        return env;
    }
    if (defaultValue.isEmpty()) {
        return QSettings().value(settingsKey).toString();
    }
    return QSettings().value(settingsKey, defaultValue).toString();
}

} // namespace

AppSettings &AppSettings::instance()
{
    static AppSettings s;
    return s;
}

AppSettings::AppSettings()
{
    QSettings settings;
    if (!settings.contains(QStringLiteral("deviceName"))) {
        settings.setValue(QStringLiteral("deviceName"), QStringLiteral("Darpan PC"));
    }
    if (!settings.contains(QStringLiteral("signalingUrl"))) {
        settings.setValue(QStringLiteral("signalingUrl"), darpan::defaultSignalingUrl());
    }
    if (!settings.contains(QStringLiteral("stunServer"))) {
        settings.setValue(QStringLiteral("stunServer"), darpan::defaultStunServer());
    }
}

QString AppSettings::deviceName() const
{
    return envOrSettings("DARPAN_DEVICE_NAME", QStringLiteral("deviceName"), QStringLiteral("Darpan PC"));
}

void AppSettings::setDeviceName(const QString &name)
{
    QSettings().setValue(QStringLiteral("deviceName"), name);
}

QString AppSettings::signalingUrl() const
{
    return envOrSettings("DARPAN_SIGNALING_URL", QStringLiteral("signalingUrl"), darpan::defaultSignalingUrl());
}

void AppSettings::setSignalingUrl(const QString &url)
{
    QSettings().setValue(QStringLiteral("signalingUrl"), url);
}

QString AppSettings::stunServer() const
{
    return envOrSettings("DARPAN_STUN_URL", QStringLiteral("stunServer"), darpan::defaultStunServer());
}

void AppSettings::setStunServer(const QString &stun)
{
    QSettings().setValue(QStringLiteral("stunServer"), stun);
}

QString AppSettings::turnServer() const
{
    return envOrSettings("DARPAN_TURN_URL", QStringLiteral("turnServer"));
}

void AppSettings::setTurnServer(const QString &turn)
{
    QSettings().setValue(QStringLiteral("turnServer"), turn);
}

QString AppSettings::turnUsername() const
{
    return envOrSettings("DARPAN_TURN_USERNAME", QStringLiteral("turnUsername"));
}

void AppSettings::setTurnUsername(const QString &user)
{
    QSettings().setValue(QStringLiteral("turnUsername"), user);
}

QString AppSettings::turnPassword() const
{
    return envOrSettings("DARPAN_TURN_PASSWORD", QStringLiteral("turnPassword"));
}

void AppSettings::setTurnPassword(const QString &password)
{
    QSettings().setValue(QStringLiteral("turnPassword"), password);
}

bool AppSettings::signalingTlsPinEnabled() const
{
    if (qEnvironmentVariableIsSet("DARPAN_SIGNALING_PIN_SHA256")) {
        return true;
    }
    return QSettings().value(QStringLiteral("signalingTlsPinEnabled"), false).toBool();
}

void AppSettings::setSignalingTlsPinEnabled(bool enabled)
{
    QSettings().setValue(QStringLiteral("signalingTlsPinEnabled"), enabled);
}

QString AppSettings::signalingTlsPinSha256() const
{
    const QString env = qEnvironmentVariable("DARPAN_SIGNALING_PIN_SHA256");
    if (!env.isEmpty()) {
        return env.trimmed();
    }
    return QSettings().value(QStringLiteral("signalingTlsPinSha256")).toString().trimmed();
}

void AppSettings::setSignalingTlsPinSha256(const QString &sha256Hex)
{
    QSettings().setValue(QStringLiteral("signalingTlsPinSha256"), sha256Hex.trimmed());
}

QVector<RecentRoomEntry> AppSettings::recentRooms() const
{
    QVector<RecentRoomEntry> out;
    const QByteArray raw = QSettings().value(QStringLiteral("recentRoomsJson")).toByteArray();
    if (raw.isEmpty()) {
        return out;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(raw);
    if (!doc.isArray()) {
        return out;
    }
    for (const QJsonValue &v : doc.array()) {
        if (!v.isObject()) {
            continue;
        }
        const QJsonObject obj = v.toObject();
        RecentRoomEntry e;
        e.roomId = obj.value(QStringLiteral("room_id")).toString();
        e.pinRequired = obj.value(QStringLiteral("pin_required")).toBool(false);
        e.lastUsedEpochMs = qint64(obj.value(QStringLiteral("last_used_ms")).toDouble(0));
        if (!e.roomId.isEmpty()) {
            out.append(e);
        }
    }
    return out;
}

void AppSettings::addRecentRoom(const QString &roomId, bool pinRequired)
{
    const QString id = roomId.trimmed().toUpper();
    if (id.isEmpty()) {
        return;
    }

    QVector<RecentRoomEntry> entries = recentRooms();
    entries.erase(std::remove_if(entries.begin(), entries.end(),
                                 [&id](const RecentRoomEntry &e) { return e.roomId.compare(id, Qt::CaseInsensitive) == 0; }),
                  entries.end());

    RecentRoomEntry fresh;
    fresh.roomId = id;
    fresh.pinRequired = pinRequired;
    fresh.lastUsedEpochMs = QDateTime::currentMSecsSinceEpoch();
    entries.prepend(fresh);
    while (entries.size() > 8) {
        entries.removeLast();
    }

    QJsonArray arr;
    for (const RecentRoomEntry &e : entries) {
        QJsonObject obj;
        obj.insert(QStringLiteral("room_id"), e.roomId);
        obj.insert(QStringLiteral("pin_required"), e.pinRequired);
        obj.insert(QStringLiteral("last_used_ms"), double(e.lastUsedEpochMs));
        arr.append(obj);
    }
    QSettings().setValue(QStringLiteral("recentRoomsJson"), QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

void AppSettings::removeRecentRoom(const QString &roomId)
{
    const QString id = roomId.trimmed().toUpper();
    if (id.isEmpty()) {
        return;
    }

    QVector<RecentRoomEntry> entries = recentRooms();
    const qsizetype before = entries.size();
    entries.erase(std::remove_if(entries.begin(), entries.end(),
                                 [&id](const RecentRoomEntry &e) {
                                     return e.roomId.compare(id, Qt::CaseInsensitive) == 0;
                                 }),
                  entries.end());
    if (entries.size() == before) {
        return;
    }

    QJsonArray arr;
    for (const RecentRoomEntry &e : entries) {
        QJsonObject obj;
        obj.insert(QStringLiteral("room_id"), e.roomId);
        obj.insert(QStringLiteral("pin_required"), e.pinRequired);
        obj.insert(QStringLiteral("last_used_ms"), double(e.lastUsedEpochMs));
        arr.append(obj);
    }
    QSettings().setValue(QStringLiteral("recentRoomsJson"), QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

void AppSettings::sync()
{
    QSettings().sync();
}

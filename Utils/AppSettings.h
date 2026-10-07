#pragma once



#include <QString>

#include <QVector>



struct RecentRoomEntry

{

    QString roomId;

    bool pinRequired = false;

    qint64 lastUsedEpochMs = 0;

};



class AppSettings

{

public:

    static AppSettings &instance();



    QString deviceName() const;

    void setDeviceName(const QString &name);



    QString signalingUrl() const;

    void setSignalingUrl(const QString &url);



    QString stunServer() const;

    void setStunServer(const QString &stun);



    QString turnServer() const;

    void setTurnServer(const QString &turn);



    QString turnUsername() const;

    void setTurnUsername(const QString &user);



    QString turnPassword() const;

    void setTurnPassword(const QString &password);



    bool signalingTlsPinEnabled() const;

    void setSignalingTlsPinEnabled(bool enabled);



    QString signalingTlsPinSha256() const;

    void setSignalingTlsPinSha256(const QString &sha256Hex);



    QVector<RecentRoomEntry> recentRooms() const;

    void addRecentRoom(const QString &roomId, bool pinRequired);
    void removeRecentRoom(const QString &roomId);



    void sync();



private:

    AppSettings();

};



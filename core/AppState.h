#pragma once

#include <QObject>
#include <QDateTime>
#include <QList>
#include <QMap>
#include <QMutex>
#include <QMutexLocker>
#include <QReadWriteLock>
#include <QStringList>
#include <QAtomicPointer>
#include <QtGlobal>
#include <nlohmann/json.hpp>

// ===========================================================================
// Plain data structures
// ===========================================================================

struct ConnectionConfig {
    QString serverUrl;
    QString roomId;
    QString password;
    QString appType;   ///< "host" | "viewer" | "controller"
    bool    rememberMe{ false };
};

struct PeerInfo {
    QString        id;           ///< server-assigned peer id
    QString        userId;
    QString        displayName;
    QString        appType;      ///< "host" | "viewer" | "controller"
    QString        role;
    QDateTime      joinedAt;
    nlohmann::json metadata;     ///< arbitrary extra fields from server
};

struct VideoStats {
    int    fps{ 0 };
    int    bitrate{ 0 };   ///< kbps
    int    width{ 0 };
    int    height{ 0 };
    double latency{ 0.0 }; ///< ms
};

struct RoomInfo {
    QString              roomId;
    QString              localPeerId;
    QString              producerType; ///< "video" | "data" | …
    bool                 streamReady{ false };
    QList<PeerInfo>      peers;
};

struct AppSettings {
    // Connection
    QString serverUrl{ QStringLiteral("ws://localhost:3000") };
    QString stunServer{ QStringLiteral("stun:stun.l.google.com:19302") };
    QString turnServer;
    QString turnUsername;
    QString turnPassword;
    int     connectionTimeoutSec{ 15 };
    bool    autoReconnect{ true };
    int     maxReconnectAttempts{ 5 };

    // Video
    int     monitorIndex{ 0 };
    int     targetFps{ 60 };
    int     bitrateKbps{ 12000 };  // 12 Mbps – sharp text at 1080p60
    QString codec{ QStringLiteral("H264") }; ///< "H264" | "VP8"
    int   previewQuality{ 75 };

    // Input control
    bool        keyboardEnabled{ true };
    bool        mouseEnabled{ true };
    int         mouseSensitivity{ 50 };
    QStringList blockedKeys;
};

struct ControlEvent {
    enum class Type { Control, Keyboard, Mouse };

    Type        type{};
    QString     senderId;    ///< originating peer id
    QString     key;         ///< key name for keyboard events
    QString     command;     ///< command name for control events
    QString     button;      ///< "left" | "right" | "middle" for mouse events
    float       x{ 0.f };   ///< normalised [0,1]
    float       y{ 0.f };
    float       deltaX{ 0.f };
    float       deltaY{ 0.f };
    QStringList modifiers;   ///< "ctrl", "alt", "shift", "meta"
    QString     eventType;   ///< "keydown" | "keyup" | "mousemove" | "mousedown" …
};

// ===========================================================================
// AppState  –  singleton QObject carrying all live application state
// ===========================================================================

class AppState : public QObject
{
    Q_OBJECT

public:
    // ── Connection state machine ──────────────────────────────────────────
    enum class ConnectionState {
        Disconnected,
        Connecting,
        Connected,
        Reconnecting,
        Error
    };
    Q_ENUM(ConnectionState)

        // ── Singleton access ─────────────────────────────────────────────────
        /// Returns the process-wide AppState instance.
        /// Thread-safe: first call initialises, subsequent calls return same ptr.
        static AppState* instance()
    {
        // Relaxed load is safe: Q_GLOBAL_STATIC guarantees one-time init.
        AppState* p = s_instance.loadRelaxed();
        if (!p) {
            static QMutex initMutex;
            QMutexLocker lock(&initMutex);
            p = s_instance.loadAcquire();
            if (!p) {
                p = new AppState();
                s_instance.storeRelease(p);
            }
        }
        return p;
    }

    // ── Accessors (thread-safe read) ──────────────────────────────────────

    ConnectionConfig connectionConfig() const
    {
        QReadLocker lock(&m_lock);
        return m_connectionConfig;
    }

    RoomInfo roomInfo() const
    {
        QReadLocker lock(&m_lock);
        return m_roomInfo;
    }

    VideoStats videoStats() const
    {
        QReadLocker lock(&m_lock);
        return m_videoStats;
    }

    AppSettings appSettings() const
    {
        QReadLocker lock(&m_lock);
        return m_appSettings;
    }

    ConnectionState connectionState() const
    {
        QReadLocker lock(&m_lock);
        return m_connectionState;
    }

    // ── Mutators (thread-safe write + signal emission) ────────────────────

    void setConnectionConfig(const ConnectionConfig& cfg)
    {
        {
            QWriteLocker lock(&m_lock);
            m_connectionConfig = cfg;
        }
        // No dedicated signal for config – callers set before connecting.
    }

    void setRoomInfo(const RoomInfo& info)
    {
        {
            QWriteLocker lock(&m_lock);
            m_roomInfo = info;
        }
        emit roomInfoChanged(info);
    }

    void setVideoStats(const VideoStats& stats)
    {
        {
            QWriteLocker lock(&m_lock);
            m_videoStats = stats;
        }
        emit videoStatsChanged(stats);
    }

    void setAppSettings(const AppSettings& settings)
    {
        {
            QWriteLocker lock(&m_lock);
            m_appSettings = settings;
        }
        emit appSettingsChanged(settings);
    }

    void setConnectionState(ConnectionState state)
    {
        {
            QWriteLocker lock(&m_lock);
            m_connectionState = state;
        }
        emit connectionStateChanged(state);
    }

    // Convenience: mutate a single peer in RoomInfo without a full copy
    void addPeer(const PeerInfo& peer)
    {
        RoomInfo snapshot;
        {
            QWriteLocker lock(&m_lock);
            // Remove any stale entry with the same id first
            m_roomInfo.peers.removeIf(
                [&peer](const PeerInfo& p) { return p.id == peer.id; });
            m_roomInfo.peers.append(peer);
            snapshot = m_roomInfo;
        }
        emit roomInfoChanged(snapshot);
    }

    void removePeer(const QString& peerId)
    {
        RoomInfo snapshot;
        {
            QWriteLocker lock(&m_lock);
            m_roomInfo.peers.removeIf(
                [&peerId](const PeerInfo& p) { return p.id == peerId; });
            snapshot = m_roomInfo;
        }
        emit roomInfoChanged(snapshot);
    }

signals:
    void roomInfoChanged(const RoomInfo& info);
    void videoStatsChanged(const VideoStats& stats);
    void appSettingsChanged(const AppSettings& settings);
    void connectionStateChanged(AppState::ConnectionState state);

private:
    explicit AppState(QObject* parent = nullptr) : QObject(parent) {}

    // Prevent accidental copies
    AppState(const AppState&) = delete;
    AppState& operator=(const AppState&) = delete;

    mutable QReadWriteLock m_lock{ QReadWriteLock::NonRecursive };

    ConnectionConfig  m_connectionConfig;
    RoomInfo          m_roomInfo;
    VideoStats        m_videoStats;
    AppSettings       m_appSettings;
    ConnectionState   m_connectionState{ ConnectionState::Disconnected };

    static QAtomicPointer<AppState> s_instance;
};

// ---------------------------------------------------------------------------
// Static member definition (header-only: inline since C++17)
// ---------------------------------------------------------------------------
inline QAtomicPointer<AppState> AppState::s_instance{};

// ---------------------------------------------------------------------------
// Convenience macro – avoids verbose AppState::instance()-> everywhere
// ---------------------------------------------------------------------------
#define APP_STATE AppState::instance()
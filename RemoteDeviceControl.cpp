#include "RemoteDeviceControl.h"

#include "ui/AppShell.h"
#include "ui/ConnectModal.h"
#include "ui/ViewerPage.h"
#include "ui/HostPage.h"
#include "core/AppController.h"
#include "core/AppState.h"

#include <QSettings>

// ─────────────────────────────────────────────────────────────────────────────
// RemoteDeviceControl  –  application bootstrapper
// ─────────────────────────────────────────────────────────────────────────────

RemoteDeviceControl::RemoteDeviceControl(QObject* parent)
    : QObject(parent)
{
    // ── 1. Main window ────────────────────────────────────────────────────
    m_shell = new AppShell();
    m_shell->setWindowTitle(QStringLiteral("RemoteControl"));

    // ── 2. Page widgets ───────────────────────────────────────────────────
    m_connectModal = new ConnectModal(m_shell);
    m_connectModal->hide();

    m_viewerPage = new ViewerPage();
    m_hostPage = new HostPage();

    m_shell->setPageWidget(PageType::Viewer, m_viewerPage);
    m_shell->setPageWidget(PageType::Host, m_hostPage);

    // ── 3. Load persisted settings into AppState ──────────────────────────
    {
        QSettings s(QStringLiteral("Deskshare"), QStringLiteral("RemoteControl"));
        AppSettings as;

        s.beginGroup(QStringLiteral("Connection"));
        as.serverUrl = s.value(QStringLiteral("serverUrl"), as.serverUrl).toString();
        as.stunServer = s.value(QStringLiteral("stunServer"), as.stunServer).toString();
        as.turnServer = s.value(QStringLiteral("turnServer"), as.turnServer).toString();
        as.connectionTimeoutSec = s.value(QStringLiteral("connectionTimeoutSec"), as.connectionTimeoutSec).toInt();
        as.autoReconnect = s.value(QStringLiteral("autoReconnect"), as.autoReconnect).toBool();
        as.maxReconnectAttempts = s.value(QStringLiteral("maxReconnectAttempts"), as.maxReconnectAttempts).toInt();
        s.endGroup();

        s.beginGroup(QStringLiteral("Video"));
        as.monitorIndex = s.value(QStringLiteral("monitorIndex"), as.monitorIndex).toInt();
        as.targetFps = s.value(QStringLiteral("targetFps"), as.targetFps).toInt();
        as.bitrateKbps = s.value(QStringLiteral("bitrateKbps"), as.bitrateKbps).toInt();
        as.codec = s.value(QStringLiteral("codec"), as.codec).toString();
        as.previewQuality = s.value(QStringLiteral("previewQuality"), as.previewQuality).toInt();
        s.endGroup();

        s.beginGroup(QStringLiteral("Input"));
        as.keyboardEnabled = s.value(QStringLiteral("keyboardEnabled"), as.keyboardEnabled).toBool();
        as.mouseEnabled = s.value(QStringLiteral("mouseEnabled"), as.mouseEnabled).toBool();
        as.mouseSensitivity = s.value(QStringLiteral("mouseSensitivity"), as.mouseSensitivity).toInt();
        as.blockedKeys = s.value(QStringLiteral("blockedKeys"), as.blockedKeys).toStringList();
        s.endGroup();

        APP_STATE->setAppSettings(as);
    }

    // ── 4. Controller ─────────────────────────────────────────────────────
    m_controller = new AppController(m_shell, this);

    // ── 5. Forward AppState changes → shell status indicator ─────────────
    QObject::connect(APP_STATE, &AppState::connectionStateChanged,
        m_shell, [this](AppState::ConnectionState state) {
            switch (state) {
            case AppState::ConnectionState::Connected:
                m_shell->setConnectionStatus(AppShell::ConnectionStatus::Connected);
                break;
            case AppState::ConnectionState::Connecting:
            case AppState::ConnectionState::Reconnecting:
                m_shell->setConnectionStatus(AppShell::ConnectionStatus::Connecting);
                break;
            default:
                m_shell->setConnectionStatus(AppShell::ConnectionStatus::Disconnected);
                break;
            }
        }, Qt::QueuedConnection);

    // ── 6. Forward VideoStats → ViewerPage HUD ────────────────────────────
    QObject::connect(APP_STATE, &AppState::videoStatsChanged,
        m_viewerPage, [this](const VideoStats& vs) {
            ConnectionInfo ci;
            ci.fps = vs.fps;
            ci.bitrateKbps = vs.bitrate;
            ci.width = vs.width;
            ci.height = vs.height;
            ci.latencyMs = static_cast<int>(vs.latency);
            m_viewerPage->setConnectionInfo(ci);
        }, Qt::QueuedConnection);

    // ── 7. Forward RoomInfo peer changes → HostPage ───────────────────────
    QObject::connect(APP_STATE, &AppState::roomInfoChanged,
        m_hostPage, [this](const RoomInfo& info) {
            m_hostPage->setStreamStatus(info.streamReady, info.peers.size());
        }, Qt::QueuedConnection);
}

RemoteDeviceControl::~RemoteDeviceControl()
{
    delete m_shell;
}

void RemoteDeviceControl::launch()
{
    m_shell->show();
    m_controller->start();
}
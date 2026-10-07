#include "darpan.h"

#include "RemoteSession/SessionCoordinator.h"
#include "Ui/FileTransferPanel.h"
#include "Ui/VideoDisplayWidget.h"
#include "Utils/AppSettings.h"
#include "include/AppConstants.h"

#include <QApplication>
#include <QButtonGroup>
#include <QClipboard>
#include <QColor>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QSplitter>
#include <QTabWidget>
#include <QTabBar>
#include <QVBoxLayout>

namespace {

QWidget *wrapCard(QWidget *inner)
{
    auto *card = new QFrame;
    card->setObjectName(QStringLiteral("ContentCard"));
    auto *lay = new QVBoxLayout(card);
    lay->setContentsMargins(24, 24, 24, 24);
    lay->setSpacing(16);
    lay->addWidget(inner);
    return card;
}

} // namespace

DarpanMainWindow::DarpanMainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    // UI stack: Qt Widgets + QSS (Phase 1 choice). QML was considered; Widgets keeps
    // desktop parity with dictation-style module layout and simpler libdatachannel threading.

    setObjectName(QStringLiteral("DarpanRoot"));
    setWindowTitle(QStringLiteral("Darpan"));
    resize(1080, 720);

    loadStyleSheet();

    auto *central = new QWidget(this);
    central->setObjectName(QStringLiteral("DarpanRoot"));
    auto *rootLayout = new QHBoxLayout(central);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    m_fileTransferPanel = new FileTransferPanel(this);

    m_stack = new QStackedWidget;
    m_stack->addWidget(buildPageHost());
    m_stack->addWidget(buildPageJoin());
    m_stack->addWidget(buildPageSettings());
    m_pageRoomIndex = m_stack->addWidget(buildPageRoom());

    auto *navGroup = new QButtonGroup(this);
    navGroup->setExclusive(true);
    m_navGroup = navGroup;

    m_navRail = qobject_cast<QFrame *>(buildNavRail(navGroup, m_stack));

    rootLayout->addWidget(m_navRail);

    auto *contentColumn = new QVBoxLayout;
    contentColumn->setContentsMargins(24, 24, 24, 16);
    contentColumn->addWidget(m_stack, 1);

    m_statusLabel = new QLabel(tr("Connect to signaling from Host or Join."));
    m_statusLabel->setObjectName(QStringLiteral("PageSubtitle"));
    contentColumn->addWidget(m_statusLabel);

    m_contentColumn = new QWidget;
    m_contentColumn->setLayout(contentColumn);

    rootLayout->addWidget(m_contentColumn, 1);
    setCentralWidget(central);

    m_coordinator = new darpan::SessionCoordinator(this);
    m_coordinator->connectSignaling();

    connect(m_hostCreateBtn, &QPushButton::clicked, this, [this]() {
        m_coordinator->hostCreateRoom(m_hostPinEdit->text());
    });
    connect(m_joinBtn, &QPushButton::clicked, this, [this]() {
        m_coordinator->joinRoom(m_joinCodeEdit->text(), m_joinPinEdit->text());
    });
    connect(m_leaveRoomBtn, &QPushButton::clicked, m_coordinator, &darpan::SessionCoordinator::leaveRoom);
    connect(m_reconnectSignalBtn, &QPushButton::clicked, m_coordinator,
            &darpan::SessionCoordinator::reconnectSignaling);
    connect(m_reconnectPeerBtn, &QPushButton::clicked, m_coordinator,
            &darpan::SessionCoordinator::reconnectPeerVideo);
    connect(m_copyRoomCodeBtn, &QPushButton::clicked, this, [this]() {
        QApplication::clipboard()->setText(m_roomCodeLabel->text());
        setStatusText(tr("Room code copied"));
    });
    connect(m_requestControlBtn, &QPushButton::clicked, m_coordinator,
            &darpan::SessionCoordinator::requestRemoteControl);
    connect(m_sendFileBtn, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(this, tr("Send file"));
        if (!path.isEmpty()) {
            m_coordinator->sendFile(path);
            if (m_roomSessionTabs) {
                m_roomSessionTabs->setCurrentIndex(1);
            }
        }
    });
    connect(m_revokeControlBtn, &QPushButton::clicked, m_coordinator,
            &darpan::SessionCoordinator::revokeActiveController);
    connect(m_toggleScreenShareBtn, &QPushButton::clicked, m_coordinator,
            &darpan::SessionCoordinator::toggleHostScreenSharing);
    connect(m_fullscreenVideoBtn, &QPushButton::clicked, m_videoWidget, &VideoDisplayWidget::toggleFullscreen);
    connect(m_videoWidget, &VideoDisplayWidget::fullscreenChanged, this, [this](bool active) {
        if (m_fullscreenVideoBtn) {
            m_fullscreenVideoBtn->setText(active ? tr("Exit full screen (Esc)") : tr("Full screen view"));
        }
    });
    connect(m_fileTransferPanel, &FileTransferPanel::cancelRequested, m_coordinator,
            &darpan::SessionCoordinator::cancelFileTransfer);

    refreshRecentRoomCards();
}

void DarpanMainWindow::closeEvent(QCloseEvent *event)
{
    if (m_coordinator) {
        m_coordinator->shutdownOnExit();
    }
    QMainWindow::closeEvent(event);
}

void DarpanMainWindow::updateHostScreenShareButton(bool sharingActive)
{
    if (!m_toggleScreenShareBtn) {
        return;
    }
    if (sharingActive) {
        m_toggleScreenShareBtn->setText(tr("Stop screen sharing"));
        m_toggleScreenShareBtn->setObjectName(QStringLiteral("DangerButton"));
        m_toggleScreenShareBtn->style()->unpolish(m_toggleScreenShareBtn);
        m_toggleScreenShareBtn->style()->polish(m_toggleScreenShareBtn);
    } else {
        m_toggleScreenShareBtn->setText(tr("Start screen sharing"));
        m_toggleScreenShareBtn->setObjectName(QStringLiteral("PrimaryButton"));
        m_toggleScreenShareBtn->style()->unpolish(m_toggleScreenShareBtn);
        m_toggleScreenShareBtn->style()->polish(m_toggleScreenShareBtn);
    }
}

void DarpanMainWindow::setRoomHostUi(bool isHost)
{
    if (m_toggleScreenShareBtn) {
        m_toggleScreenShareBtn->setVisible(isHost);
    }
    m_requestControlBtn->setVisible(!isHost);
    m_revokeControlBtn->setVisible(isHost);
    if (m_fullscreenVideoBtn) {
        m_fullscreenVideoBtn->setVisible(!isHost);
    }
    m_videoWidget->setPreviewMode(isHost ? VideoDisplayWidget::PreviewMode::HostLocal
                                         : VideoDisplayWidget::PreviewMode::RemotePeer);
    if (m_videoWidget->frame().isNull()) {
        m_videoWidget->showPlaceholder();
    }
}

void DarpanMainWindow::setSessionSharingIndicator(bool active, const QString &message)
{
    if (!m_privacyBanner) {
        return;
    }
    m_privacyBanner->setVisible(active);
    m_privacyBanner->setText(message);
}

bool DarpanMainWindow::confirmGrantControl(const QString &peerDisplayName)
{
    return QMessageBox::question(
               this, tr("Allow remote control?"),
               tr("%1 is requesting control of this PC.\nAllow mouse and keyboard injection?").arg(peerDisplayName))
        == QMessageBox::Yes;
}

void DarpanMainWindow::updateControlButtons(bool isHost, bool selfControlGranted, bool peerHasControl,
                                            bool requestPending)
{
    m_requestControlBtn->setVisible(!isHost);
    m_revokeControlBtn->setVisible(isHost);

    if (isHost) {
        m_revokeControlBtn->setEnabled(peerHasControl);
        m_revokeControlBtn->setText(peerHasControl ? tr("Revoke control")
                                                   : tr("No active control"));
        return;
    }

    if (selfControlGranted) {
        m_requestControlBtn->setText(tr("Control active"));
        m_requestControlBtn->setEnabled(false);
    } else if (requestPending) {
        m_requestControlBtn->setText(tr("Waiting for host…"));
        m_requestControlBtn->setEnabled(false);
    } else {
        m_requestControlBtn->setText(tr("Request control"));
        m_requestControlBtn->setEnabled(true);
    }
}

void DarpanMainWindow::setCaptureBackendText(const QString &backend)
{
    m_captureBackendLabel->setText(tr("Capture: %1 (QT default; set DARPAN_CAPTURE_DXGI=1 for DXGI)")
                                       .arg(backend.toUpper()));
}

void DarpanMainWindow::recordRecentRoom(const QString &roomId, bool pinRequired)
{
    AppSettings::instance().addRecentRoom(roomId, pinRequired);
    refreshRecentRoomCards();
}

void DarpanMainWindow::refreshRecentRoomCards()
{
    if (!m_recentRoomsLayout) {
        return;
    }
    while (QLayoutItem *item = m_recentRoomsLayout->takeAt(0)) {
        if (QWidget *w = item->widget()) {
            w->deleteLater();
        }
        delete item;
    }

    const QVector<RecentRoomEntry> recents = AppSettings::instance().recentRooms();
    if (recents.isEmpty()) {
        auto *empty = new QLabel(tr("No recent rooms yet — create or join a session."));
        empty->setObjectName(QStringLiteral("PageSubtitle"));
        empty->setWordWrap(true);
        m_recentRoomsLayout->addWidget(empty);
        return;
    }

    for (const RecentRoomEntry &entry : recents) {
        auto *card = new QFrame;
        card->setObjectName(QStringLiteral("ContentCard"));
        auto *row = new QHBoxLayout(card);
        const QDateTime when = QDateTime::fromMSecsSinceEpoch(entry.lastUsedEpochMs);
        auto *text = new QLabel(QStringLiteral("%1 · %2")
                                    .arg(entry.roomId, when.isValid() ? when.toString(Qt::ISODate) : QString()));
        text->setObjectName(QStringLiteral("PageSubtitle"));
        auto *joinBtn = new QPushButton(entry.pinRequired ? tr("Join…") : tr("Rejoin"));
        joinBtn->setObjectName(QStringLiteral("SecondaryButton"));
        const QString roomId = entry.roomId;
        const bool pinRequired = entry.pinRequired;
        connect(joinBtn, &QPushButton::clicked, this, [this, roomId, pinRequired]() {
            joinRecentRoom(roomId, pinRequired);
        });
        row->addWidget(text, 1);
        row->addWidget(joinBtn);
        m_recentRoomsLayout->addWidget(card);
    }
    m_recentRoomsLayout->addStretch();
}

void DarpanMainWindow::joinRecentRoom(const QString &roomId, bool pinRequired)
{
    if (pinRequired) {
        m_joinCodeEdit->setText(roomId);
        if (m_navGroup && m_navGroup->button(1)) {
            m_navGroup->button(1)->setChecked(true);
        }
        m_stack->setCurrentIndex(1);
        setStatusText(tr("Enter the room PIN and tap Join room."));
        return;
    }
    m_coordinator->joinRoom(roomId, {});
}

void DarpanMainWindow::setStatusText(const QString &text)
{
    m_statusLabel->setText(text);
}

void DarpanMainWindow::showRoomPage(const QString &roomId, bool isHost)
{
    m_roomCodeLabel->setText(roomId);
    if (m_navRail) {
        m_navRail->setVisible(false);
    }
    if (m_roomSessionTabs) {
        m_roomSessionTabs->setCurrentIndex(0);
    }
    setRoomHostUi(isHost);
    if (isHost) {
        updateHostScreenShareButton(true);
    }
    m_stack->setCurrentIndex(m_pageRoomIndex);
    updateControlButtons(isHost, false, false, false);
}

void DarpanMainWindow::showHomePages()
{
    m_videoWidget->exitFullscreenIfActive();
    m_stack->setCurrentIndex(0);
    m_videoWidget->setPreviewMode(VideoDisplayWidget::PreviewMode::RemotePeer);
    m_videoWidget->clearFrame();
    if (m_navRail) {
        m_navRail->setVisible(true);
    }
    if (m_fullscreenVideoBtn) {
        m_fullscreenVideoBtn->setVisible(false);
    }
    if (m_navGroup && m_navGroup->button(0)) {
        m_navGroup->button(0)->setChecked(true);
    }
    refreshRecentRoomCards();
}

void DarpanMainWindow::setRoomMembers(const QVector<RoomMemberInfo> &members, const QString &selfDeviceId)
{
    m_memberList->clear();
    for (const RoomMemberInfo &member : members) {
        QString control;
        if (member.role == QStringLiteral("host")) {
            control = tr("Host");
        } else if (member.controlState == QStringLiteral("control_granted")) {
            control = tr("Control granted");
        } else {
            control = tr("View only");
        }
        const QString line = QStringLiteral("%1 — %2 (%3)")
                                 .arg(member.displayName, member.role, control);
        auto *item = new QListWidgetItem(line);
        if (member.deviceId == selfDeviceId) {
            item->setForeground(QColor(QStringLiteral("#2dd4bf")));
        }
        m_memberList->addItem(item);
    }
}

void DarpanMainWindow::setRemoteVideoFrame(const QImage &frame)
{
    m_videoWidget->setFrame(frame);
}

void DarpanMainWindow::setPeerReconnectVisible(bool visible)
{
    m_reconnectPeerBtn->setVisible(visible);
}

void DarpanMainWindow::loadStyleSheet()
{
    QFile file(QStringLiteral(":/Ui/styles/darpan_dark.qss"));
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        setStyleSheet(QString::fromUtf8(file.readAll()));
    }
}

QWidget *DarpanMainWindow::buildNavRail(QButtonGroup *group, QStackedWidget *stack)
{
    auto *rail = new QFrame;
    rail->setObjectName(QStringLiteral("NavRail"));

    auto *layout = new QVBoxLayout(rail);
    layout->setContentsMargins(12, 16, 12, 16);
    layout->setSpacing(8);

    auto *brand = new QLabel(QStringLiteral("Darpan"));
    brand->setObjectName(QStringLiteral("BrandTitle"));
    layout->addWidget(brand);

    const struct NavItem {
        const char *label;
        int index;
    } items[] = {
        {QT_TR_NOOP("Host session"), 0},
        {QT_TR_NOOP("Join session"), 1},
        {QT_TR_NOOP("Settings"), 2},
    };

    for (const NavItem &item : items) {
        auto *btn = new QPushButton(tr(item.label));
        btn->setProperty("nav", true);
        btn->setCheckable(true);
        group->addButton(btn, item.index);
        layout->addWidget(btn);
        if (item.index == 0) {
            btn->setChecked(true);
        }
    }

    layout->addStretch();
    connect(group, &QButtonGroup::idClicked, stack, [this, stack](int id) {
        if (m_stack->currentIndex() == m_pageRoomIndex) {
            return;
        }
        stack->setCurrentIndex(id);
    });

    return rail;
}

QWidget *DarpanMainWindow::buildPageHost()
{
    auto *inner = new QWidget;
    auto *lay = new QVBoxLayout(inner);
    lay->setSpacing(12);

    auto *title = new QLabel(tr("Host a session"));
    title->setObjectName(QStringLiteral("PageTitle"));
    auto *sub = new QLabel(tr("Create a room, share the code, and stream your primary monitor when a peer connects."));
    sub->setObjectName(QStringLiteral("PageSubtitle"));
    sub->setWordWrap(true);

    m_hostPinEdit = new QLineEdit;
    m_hostPinEdit->setPlaceholderText(tr("Optional room PIN"));

    m_hostCreateBtn = new QPushButton(tr("Create room"));
    m_hostCreateBtn->setObjectName(QStringLiteral("PrimaryButton"));

    lay->addWidget(title);
    lay->addWidget(sub);
    lay->addWidget(m_hostPinEdit);
    lay->addWidget(m_hostCreateBtn);

    auto *recentTitle = new QLabel(tr("Recent rooms"));
    recentTitle->setObjectName(QStringLiteral("PageTitle"));
    m_recentRoomsLayout = new QVBoxLayout;
    m_recentRoomsLayout->setSpacing(8);
    lay->addWidget(recentTitle);
    lay->addLayout(m_recentRoomsLayout);

    lay->addStretch();

    auto *page = new QWidget;
    auto *pageLay = new QVBoxLayout(page);
    pageLay->addWidget(wrapCard(inner));
    return page;
}

QWidget *DarpanMainWindow::buildPageJoin()
{
    auto *inner = new QWidget;
    auto *lay = new QVBoxLayout(inner);
    lay->setSpacing(12);

    auto *title = new QLabel(tr("Join a session"));
    title->setObjectName(QStringLiteral("PageTitle"));

    m_joinCodeEdit = new QLineEdit;
    m_joinCodeEdit->setPlaceholderText(tr("Room code"));
    m_joinPinEdit = new QLineEdit;
    m_joinPinEdit->setPlaceholderText(tr("Room PIN (if required)"));
    m_joinPinEdit->setEchoMode(QLineEdit::Password);

    m_joinBtn = new QPushButton(tr("Join room"));
    m_joinBtn->setObjectName(QStringLiteral("PrimaryButton"));

    lay->addWidget(title);
    lay->addWidget(m_joinCodeEdit);
    lay->addWidget(m_joinPinEdit);
    lay->addWidget(m_joinBtn);
    lay->addStretch();

    auto *page = new QWidget;
    auto *pageLay = new QVBoxLayout(page);
    pageLay->addWidget(wrapCard(inner));
    return page;
}

QWidget *DarpanMainWindow::buildPageSettings()
{
    auto *inner = new QWidget;
    auto *lay = new QVBoxLayout(inner);
    lay->setSpacing(12);

    auto *title = new QLabel(tr("Settings"));
    title->setObjectName(QStringLiteral("PageTitle"));

    const AppSettings &settings = AppSettings::instance();
    m_settingsNameEdit = new QLineEdit(settings.deviceName());
    m_settingsSignalEdit = new QLineEdit(settings.signalingUrl());
    m_settingsStunEdit = new QLineEdit(settings.stunServer());
    m_settingsTurnEdit = new QLineEdit(settings.turnServer());
    m_settingsTurnUserEdit = new QLineEdit(settings.turnUsername());
    m_settingsTurnPassEdit = new QLineEdit(settings.turnPassword());
    m_settingsTurnPassEdit->setEchoMode(QLineEdit::Password);
    m_settingsPinEnable = new QCheckBox(tr("Pin signaling TLS certificate (SHA-256)"));
    m_settingsPinEnable->setChecked(settings.signalingTlsPinEnabled());
    m_settingsPinShaEdit = new QLineEdit(settings.signalingTlsPinSha256());
    m_settingsPinShaEdit->setPlaceholderText(tr("Certificate SHA-256 hex (optional)"));

    auto *saveBtn = new QPushButton(tr("Save settings"));
    saveBtn->setObjectName(QStringLiteral("SecondaryButton"));
    connect(saveBtn, &QPushButton::clicked, this, [this]() {
        AppSettings::instance().setDeviceName(m_settingsNameEdit->text());
        AppSettings::instance().setSignalingUrl(m_settingsSignalEdit->text());
        AppSettings::instance().setStunServer(m_settingsStunEdit->text());
        AppSettings::instance().setTurnServer(m_settingsTurnEdit->text());
        AppSettings::instance().setTurnUsername(m_settingsTurnUserEdit->text());
        AppSettings::instance().setTurnPassword(m_settingsTurnPassEdit->text());
        AppSettings::instance().setSignalingTlsPinEnabled(m_settingsPinEnable->isChecked());
        AppSettings::instance().setSignalingTlsPinSha256(m_settingsPinShaEdit->text());
        AppSettings::instance().sync();
        m_coordinator->connectSignaling();
        setStatusText(tr("Settings saved"));
    });

    lay->addWidget(title);
    lay->addWidget(new QLabel(tr("Device name"), inner));
    lay->addWidget(m_settingsNameEdit);
    lay->addWidget(new QLabel(tr("Signaling WebSocket URL"), inner));
    lay->addWidget(m_settingsSignalEdit);
    lay->addWidget(new QLabel(tr("STUN server"), inner));
    lay->addWidget(m_settingsStunEdit);
    lay->addWidget(new QLabel(tr("TURN server (optional)"), inner));
    lay->addWidget(m_settingsTurnEdit);
    lay->addWidget(new QLabel(tr("TURN username"), inner));
    lay->addWidget(m_settingsTurnUserEdit);
    lay->addWidget(new QLabel(tr("TURN password"), inner));
    lay->addWidget(m_settingsTurnPassEdit);
    lay->addWidget(m_settingsPinEnable);
    lay->addWidget(m_settingsPinShaEdit);
    lay->addWidget(saveBtn);
    lay->addStretch();

    auto *page = new QWidget;
    auto *pageLay = new QVBoxLayout(page);
    pageLay->addWidget(wrapCard(inner));
    return page;
}

QWidget *DarpanMainWindow::buildPageRoom()
{
    auto *page = new QWidget;
    auto *layout = new QHBoxLayout(page);

    auto *left = new QVBoxLayout;
    m_privacyBanner = new QLabel;
    m_privacyBanner->setObjectName(QStringLiteral("PrivacyBanner"));
    m_privacyBanner->setWordWrap(true);
    m_privacyBanner->setVisible(false);
    m_privacyBanner->setStyleSheet(
        QStringLiteral("background-color:#7f1d1d;color:#fecaca;padding:8px;border-radius:6px;"));
    left->addWidget(m_privacyBanner);
    auto *title = new QLabel(tr("Active room"));
    title->setObjectName(QStringLiteral("PageTitle"));
    m_roomCodeLabel = new QLabel(QStringLiteral("------"));
    m_roomCodeLabel->setObjectName(QStringLiteral("BrandTitle"));
    m_copyRoomCodeBtn = new QPushButton(tr("Copy room code"));
    m_copyRoomCodeBtn->setObjectName(QStringLiteral("SecondaryButton"));
    m_roomConnectionLabel = new QLabel(tr("Connection: idle"));
    m_roomConnectionLabel->setObjectName(QStringLiteral("PageSubtitle"));

    m_memberList = new QListWidget;
    m_leaveRoomBtn = new QPushButton(tr("Leave room"));
    m_leaveRoomBtn->setObjectName(QStringLiteral("DangerButton"));
    m_reconnectSignalBtn = new QPushButton(tr("Reconnect signaling"));
    m_reconnectSignalBtn->setObjectName(QStringLiteral("SecondaryButton"));
    m_reconnectPeerBtn = new QPushButton(tr("Reconnect video"));
    m_reconnectPeerBtn->setObjectName(QStringLiteral("PrimaryButton"));
    m_reconnectPeerBtn->setVisible(false);

    m_captureBackendLabel = new QLabel(tr("Capture: —"));
    m_captureBackendLabel->setObjectName(QStringLiteral("PageSubtitle"));
    m_requestControlBtn = new QPushButton(tr("Request control"));
    m_requestControlBtn->setObjectName(QStringLiteral("SecondaryButton"));
    m_revokeControlBtn = new QPushButton(tr("Revoke control"));
    m_revokeControlBtn->setObjectName(QStringLiteral("SecondaryButton"));
    m_sendFileBtn = new QPushButton(tr("Send file…"));
    m_sendFileBtn->setObjectName(QStringLiteral("SecondaryButton"));
    m_toggleScreenShareBtn = new QPushButton(tr("Stop screen sharing"));
    m_toggleScreenShareBtn->setObjectName(QStringLiteral("DangerButton"));
    m_toggleScreenShareBtn->setVisible(false);

    left->addWidget(title);
    left->addWidget(m_roomCodeLabel);
    left->addWidget(m_copyRoomCodeBtn);
    left->addWidget(m_roomConnectionLabel);
    left->addWidget(m_captureBackendLabel);
    left->addWidget(new QLabel(tr("Participants"), page));
    left->addWidget(m_memberList, 1);
    left->addWidget(m_requestControlBtn);
    left->addWidget(m_revokeControlBtn);
    left->addWidget(m_toggleScreenShareBtn);
    left->addWidget(m_sendFileBtn);
    left->addWidget(m_reconnectSignalBtn);
    left->addWidget(m_reconnectPeerBtn);
    left->addWidget(m_leaveRoomBtn);

    m_roomSidePanel = new QFrame;
    m_roomSidePanel->setObjectName(QStringLiteral("RoomSidePanel"));
    m_roomSidePanel->setMinimumWidth(kRoomSidePanelMinWidth);
    m_roomSidePanel->setMaximumWidth(kRoomSidePanelMaxWidth);
    auto *sidePanelLayout = new QVBoxLayout(m_roomSidePanel);
    sidePanelLayout->setContentsMargins(16, 16, 16, 16);
    sidePanelLayout->setSpacing(10);
    sidePanelLayout->addLayout(left);

    m_fullscreenVideoBtn = new QPushButton(tr("Full screen view"));
    m_fullscreenVideoBtn->setObjectName(QStringLiteral("SecondaryButton"));
    m_fullscreenVideoBtn->setVisible(false);

    m_videoWidget = new VideoDisplayWidget;
    m_videoPanel = new QWidget;
    auto *videoColumn = new QVBoxLayout(m_videoPanel);
    videoColumn->setContentsMargins(0, 0, 0, 0);
    videoColumn->setSpacing(8);
    videoColumn->addWidget(m_fullscreenVideoBtn);
    videoColumn->addWidget(m_videoWidget, 1);

    m_roomSessionTabs = new QTabWidget;
    m_roomSessionTabs->setObjectName(QStringLiteral("RoomSessionTabs"));
    m_roomSessionTabs->setDocumentMode(false);
    m_roomSessionTabs->tabBar()->setDrawBase(false);
    m_roomSessionTabs->tabBar()->setExpanding(false);
    m_roomSessionTabs->tabBar()->setUsesScrollButtons(false);
    m_roomSessionTabs->addTab(m_videoPanel, tr("Screen"));
    m_roomSessionTabs->addTab(m_fileTransferPanel, tr("File transfers"));

    m_roomSplitter = new QSplitter(Qt::Horizontal);
    m_roomSplitter->setObjectName(QStringLiteral("RoomSplitter"));
    m_roomSplitter->setHandleWidth(8);
    m_roomSplitter->setChildrenCollapsible(false);
    m_roomSplitter->addWidget(m_roomSidePanel);
    m_roomSplitter->addWidget(m_roomSessionTabs);
    m_roomSplitter->setStretchFactor(0, 0);
    m_roomSplitter->setStretchFactor(1, 1);
    m_roomSplitter->setSizes({320, 680});

    layout->addWidget(m_roomSplitter);
    return page;
}

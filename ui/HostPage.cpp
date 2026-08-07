#include "HostPage.h"
#include "DarpanIcons.h"
#include "DarpanTheme.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QTimer>
#include <QElapsedTimer>
#include <QPainter>
#include <QClipboard>
#include <QApplication>
#include <QFrame>
#include <QSizePolicy>
// ===========================================================================
// PreviewWidget
// ===========================================================================

PreviewWidget::PreviewWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(240, 135);  // 16:9 minimum
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAttribute(Qt::WA_OpaquePaintEvent);
}

void PreviewWidget::updateFrame(const QImage& frame)
{
    if (frame.isNull()) { return; }
    m_frame = frame;  // store raw; scale in paintEvent to match current size
    update();
}

void PreviewWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(0x10, 0x13, 0x1A));
    if (!m_frame.isNull()) {
        // Scale to fit, preserving aspect ratio, centered.
        const QImage scaled = m_frame.scaled(size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        const int dx = (width() - scaled.width()) / 2;
        const int dy = (height() - scaled.height()) / 2;
        p.drawImage(dx, dy, scaled);
    }
    else {
        p.setPen(QColor(0x55, 0x55, 0x88));
        p.drawText(rect(), Qt::AlignCenter, tr("No preview"));
    }
    // Border
    p.setPen(QColor(0x2a, 0x2a, 0x6e));
    p.drawRect(rect().adjusted(0, 0, -1, -1));
}

// ===========================================================================
// HostPage
// ===========================================================================

namespace {

    // Convenience: create a styled section header label
    QLabel* makeHeader(const QString& text, QWidget* parent)
    {
        auto* lbl = new QLabel(text, parent);
        lbl->setObjectName(QStringLiteral("SectionHeader"));
        return lbl;
    }

    // Horizontal separator line
    QFrame* makeSep(QWidget* parent)
    {
        auto* f = new QFrame(parent);
        f->setFrameShape(QFrame::HLine);
        f->setObjectName(QStringLiteral("Separator"));
        return f;
    }

} // namespace

HostPage::HostPage(QWidget* parent)
    : QWidget(parent)
    , m_uptimeClock(new QElapsedTimer)
    , m_uptimeTimer(new QTimer(this))
{
    buildUi();

    m_uptimeTimer->setInterval(1000);
    QObject::connect(m_uptimeTimer, &QTimer::timeout,
        this, &HostPage::onUptimeTick);
}

// ---------------------------------------------------------------------------
// UI construction
// ---------------------------------------------------------------------------

void HostPage::buildUi()
{
    auto* rootLayout = new QHBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    // Left sidebar
    auto* sidebar = new QWidget(this);
    sidebar->setObjectName(QStringLiteral("HostSidebar"));
    sidebar->setFixedWidth(300);
    auto* sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(16, 16, 16, 16);
    sidebarLayout->setSpacing(16);

    auto* sessionCard = new QFrame(sidebar);
    sessionCard->setObjectName(QStringLiteral("SessionInfoCard"));
    auto* sessionLayout = new QVBoxLayout(sessionCard);
    sessionLayout->setContentsMargins(12, 12, 12, 12);
    sessionLayout->setSpacing(10);

    auto* sessionTitle = new QLabel(tr("Session Info"), sessionCard);
    sessionTitle->setObjectName(QStringLiteral("SectionHeader"));
    sessionLayout->addWidget(sessionTitle);

    auto* roomCaption = new QLabel(tr("Room ID"), sessionCard);
    roomCaption->setObjectName(QStringLiteral("SectionCaption"));
    sessionLayout->addWidget(roomCaption);

    auto* roomRow = new QHBoxLayout();
    m_roomIdLabel = new QLabel(QStringLiteral("—"), sessionCard);
    m_roomIdLabel->setObjectName(QStringLiteral("RoomIdLabel"));
    m_roomIdLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_copyRoomIdBtn = new QPushButton(sessionCard);
    m_copyRoomIdBtn->setObjectName(QStringLiteral("SmallBtn"));
    m_copyRoomIdBtn->setIcon(DarpanIcons::icon(QStringLiteral("content_copy"), QSize(18, 18)));
    m_copyRoomIdBtn->setToolTip(tr("Copy Room ID"));
    roomRow->addWidget(m_roomIdLabel, 1);
    roomRow->addWidget(m_copyRoomIdBtn);
    sessionLayout->addLayout(roomRow);

    auto* passRow = new QHBoxLayout();
    auto* passCaption = new QLabel(tr("Password"), sessionCard);
    passCaption->setObjectName(QStringLiteral("SectionCaption"));
    m_passwordLabel = new QLabel(tr("None"), sessionCard);
    m_passwordLabel->setObjectName(QStringLiteral("SectionCaption"));
    m_togglePassBtn = new QPushButton(tr("Show"), sessionCard);
    m_togglePassBtn->setObjectName(QStringLiteral("SmallBtn"));
    m_togglePassBtn->hide();
    passRow->addWidget(passCaption);
    passRow->addStretch();
    passRow->addWidget(m_passwordLabel);
    passRow->addWidget(m_togglePassBtn);
    sessionLayout->addLayout(passRow);
    sidebarLayout->addWidget(sessionCard);

    m_peerHeaderLabel = new QLabel(tr("Connected viewers (0)"), sidebar);
    m_peerHeaderLabel->setObjectName(QStringLiteral("SectionHeader"));
    sidebarLayout->addWidget(m_peerHeaderLabel);

    m_peerList = new QListWidget(sidebar);
    m_peerList->setObjectName(QStringLiteral("PeerList"));
    m_peerList->setSelectionMode(QAbstractItemView::SingleSelection);
    sidebarLayout->addWidget(m_peerList, 1);

    m_peerEmptyLabel = new QLabel(tr("No viewers yet — share room ID"), sidebar);
    m_peerEmptyLabel->setObjectName(QStringLiteral("PeerEmptyLabel"));
    m_peerEmptyLabel->setAlignment(Qt::AlignCenter);
    m_peerEmptyLabel->setWordWrap(true);
    sidebarLayout->addWidget(m_peerEmptyLabel);
    m_peerList->hide();

    rootLayout->addWidget(sidebar);

    // Right main area
    auto* mainCol = new QVBoxLayout();
    mainCol->setContentsMargins(16, 16, 16, 0);
    mainCol->setSpacing(0);

    auto* workspace = new QFrame(this);
    workspace->setObjectName(QStringLiteral("HostWorkspace"));
    auto* workspaceLayout = new QVBoxLayout(workspace);
    workspaceLayout->setContentsMargins(16, 16, 16, 16);

    m_preview = new PreviewWidget(workspace);
    m_preview->setObjectName(QStringLiteral("HostPreviewFrame"));
    m_preview->setFixedSize(256, 144);
    auto* previewRow = new QHBoxLayout();
    previewRow->addStretch();
    previewRow->addWidget(m_preview);
    workspaceLayout->addLayout(previewRow);
    workspaceLayout->addStretch();
    mainCol->addWidget(workspace, 1);

    auto* ctrlFrame = new QWidget(this);
    ctrlFrame->setObjectName(QStringLiteral("ControlPanel"));
    ctrlFrame->setFixedHeight(80);
    auto* ctrlLayout = new QHBoxLayout(ctrlFrame);
    ctrlLayout->setContentsMargins(24, 12, 24, 12);
    ctrlLayout->setSpacing(12);

    m_shareBtn = new QPushButton(tr("Share Screen"), ctrlFrame);
    m_shareBtn->setObjectName(QStringLiteral("ToggleBtn"));
    m_shareBtn->setIcon(DarpanIcons::icon(QStringLiteral("monitor")));
    m_shareBtn->setCheckable(true);

    m_controlBtn = new QPushButton(tr("Allow Control"), ctrlFrame);
    m_controlBtn->setObjectName(QStringLiteral("ToggleBtn"));
    m_controlBtn->setIcon(DarpanIcons::icon(QStringLiteral("keyboard")));
    m_controlBtn->setCheckable(true);

    m_kickBtn = new QPushButton(tr("Kick Peer"), ctrlFrame);
    m_kickBtn->setObjectName(QStringLiteral("DangerBtn"));
    m_kickBtn->setEnabled(false);

    m_endBtn = new QPushButton(tr("End Session"), ctrlFrame);
    m_endBtn->setObjectName(QStringLiteral("EndBtn"));

    ctrlLayout->addWidget(m_shareBtn);
    ctrlLayout->addWidget(m_controlBtn);
    ctrlLayout->addStretch();
    ctrlLayout->addWidget(m_kickBtn);
    ctrlLayout->addWidget(m_endBtn);
    mainCol->addWidget(ctrlFrame);

    auto* statusBar = new QWidget(this);
    statusBar->setObjectName(QStringLiteral("StatusBar"));
    statusBar->setFixedHeight(32);
    auto* statusLayout = new QHBoxLayout(statusBar);
    statusLayout->setContentsMargins(16, 0, 16, 0);
    m_statusDot = new QLabel(statusBar);
    m_statusDot->setFixedSize(8, 8);
    m_statusDot->setObjectName(QStringLiteral("StatusDotPaused"));
    m_statusText = new QLabel(tr("Paused"), statusBar);
    m_statusText->setObjectName(QStringLiteral("StatusText"));
    m_viewerCountLabel = new QLabel(tr("0 viewers"), statusBar);
    m_viewerCountLabel->setObjectName(QStringLiteral("StatusText"));
    m_uptimeLabel = new QLabel(tr("00:00:00"), statusBar);
    m_uptimeLabel->setObjectName(QStringLiteral("UptimeLabel"));
    statusLayout->addWidget(m_statusDot);
    statusLayout->addWidget(m_statusText);
    statusLayout->addWidget(new QLabel(QStringLiteral("•"), statusBar));
    statusLayout->addWidget(m_viewerCountLabel);
    statusLayout->addStretch();
    statusLayout->addWidget(m_uptimeLabel);
    mainCol->addWidget(statusBar);

    rootLayout->addLayout(mainCol, 1);

    QObject::connect(m_shareBtn, &QPushButton::clicked, this, &HostPage::onShareClicked);
    QObject::connect(m_controlBtn, &QPushButton::clicked, this, &HostPage::onControlClicked);
    QObject::connect(m_kickBtn, &QPushButton::clicked, this, &HostPage::onKickClicked);
    QObject::connect(m_endBtn, &QPushButton::clicked, this, &HostPage::onEndSessionClicked);
    QObject::connect(m_peerList, &QListWidget::itemSelectionChanged,
        this, &HostPage::onPeerSelectionChanged);
    QObject::connect(m_copyRoomIdBtn, &QPushButton::clicked, this, &HostPage::onCopyRoomId);
    QObject::connect(m_togglePassBtn, &QPushButton::clicked, this, &HostPage::onTogglePasswordVisible);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void HostPage::updatePreviewFrame(const QImage& frame)
{
    m_preview->updateFrame(frame);
}

void HostPage::setRoomInfo(const QString& roomId,
    const QString& password,
    const QString& serverUrl)
{
    m_roomId = roomId;
    m_password = password;

    m_roomIdLabel->setText(m_roomId);
    if (!m_password.isEmpty()) {
        m_passwordLabel->setText(QStringLiteral("••••••••"));
        m_togglePassBtn->show();
    }
    else {
        m_passwordLabel->setText(tr("None"));
        m_togglePassBtn->hide();
    }

    // Start uptime clock
    m_uptimeClock->start();
    m_uptimeTimer->start();
}

void HostPage::addPeer(const PeerInfo& peer)
{
    // Avoid duplicates
    for (int i = 0; i < m_peerList->count(); ++i) {
        if (m_peerList->item(i)->data(Qt::UserRole).toString() == peer.id) {
            return;
        }
    }

    const QString label = QStringLiteral("[%1]  %2  —  joined %3")
        .arg(peer.appType)
        .arg(peer.id)
        .arg(peer.joinedAt.toString(QStringLiteral("hh:mm:ss")));

    auto* item = new QListWidgetItem(label, m_peerList);
    item->setData(Qt::UserRole, peer.id);
    m_peerList->show();
    m_peerEmptyLabel->hide();

    m_viewerCount = m_peerList->count();
    updateStatusBar();
}

void HostPage::removePeer(const QString& peerId)
{
    for (int i = m_peerList->count() - 1; i >= 0; --i) {
        if (m_peerList->item(i)->data(Qt::UserRole).toString() == peerId) {
            delete m_peerList->takeItem(i);
            break;
        }
    }
    m_viewerCount = m_peerList->count();
    if (m_viewerCount == 0) {
        m_peerList->hide();
        m_peerEmptyLabel->show();
    }
    updateStatusBar();
}

void HostPage::setSharing(bool active)
{
    m_sharing = active;
    m_shareBtn->setChecked(active);
    m_streamLive = active;
    updateStatusBar();
}

void HostPage::setControlAllowed(bool allowed)
{
    m_controlAllowed = allowed;
    m_controlBtn->setChecked(allowed);
}

void HostPage::setStreamStatus(bool live, int viewerCount)
{
    m_streamLive = live;
    m_viewerCount = viewerCount;
    updateStatusBar();
}

// ---------------------------------------------------------------------------
// Private slots
// ---------------------------------------------------------------------------

void HostPage::onShareClicked()
{
    m_sharing = m_shareBtn->isChecked();
    emit shareToggled(m_sharing);
    m_streamLive = m_sharing;
    updateStatusBar();
}

void HostPage::onControlClicked()
{
    m_controlAllowed = m_controlBtn->isChecked();
    emit controlToggled(m_controlAllowed);
}

void HostPage::onKickClicked()
{
    const QString id = selectedPeerId();
    if (!id.isEmpty()) {
        emit kickPeer(id);
    }
}

void HostPage::onEndSessionClicked()
{
    emit sessionEnded();
}

void HostPage::onPeerSelectionChanged()
{
    m_kickBtn->setEnabled(!m_peerList->selectedItems().isEmpty());
}

void HostPage::onCopyRoomId()
{
    QApplication::clipboard()->setText(m_roomId);
    m_copyRoomIdBtn->setIcon(DarpanIcons::icon(QStringLiteral("cast_connected"), QSize(18, 18)));
    QTimer::singleShot(1200, m_copyRoomIdBtn, [this]() {
        m_copyRoomIdBtn->setIcon(DarpanIcons::icon(QStringLiteral("content_copy"), QSize(18, 18)));
        });
}

void HostPage::onTogglePasswordVisible()
{
    m_passwordVisible = !m_passwordVisible;
    if (m_passwordVisible) {
        m_passwordLabel->setText(m_password.isEmpty() ? tr("(none)") : m_password);
        m_togglePassBtn->setText(tr("Hide"));
    }
    else {
        m_passwordLabel->setText(
            m_password.isEmpty()
            ? tr("(none)")
            : QString(m_password.length(), QChar(0x2022)));
        m_togglePassBtn->setText(tr("Show"));
    }
}

void HostPage::onUptimeTick()
{
    if (!m_uptimeClock) { return; }
    const qint64 secs = m_uptimeClock->elapsed() / 1000;
    const int h = static_cast<int>(secs / 3600);
    const int m = static_cast<int>((secs % 3600) / 60);
    const int s = static_cast<int>(secs % 60);
    m_uptimeLabel->setText(QStringLiteral("%1:%2:%3")
        .arg(h, 2, 10, QChar('0'))
        .arg(m, 2, 10, QChar('0'))
        .arg(s, 2, 10, QChar('0')));
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

void HostPage::updateStatusBar()
{
    if (m_streamLive) {
        m_statusDot->setObjectName(QStringLiteral("StatusDotLive"));
        m_statusText->setText(tr("Live"));
    }
    else {
        m_statusDot->setObjectName(QStringLiteral("StatusDotPaused"));
        m_statusText->setText(tr("Paused"));
    }
    // Force stylesheet re-evaluation for objectName change
    m_statusDot->style()->unpolish(m_statusDot);
    m_statusDot->style()->polish(m_statusDot);

    m_viewerCountLabel->setText(
        tr("%n viewer(s)", "", m_viewerCount));
    if (m_peerHeaderLabel) {
        m_peerHeaderLabel->setText(
            tr("Connected viewers (%1)").arg(m_viewerCount));
    }
}

QString HostPage::selectedPeerId() const
{
    const auto items = m_peerList->selectedItems();
    if (items.isEmpty()) { return {}; }
    return items.first()->data(Qt::UserRole).toString();
}
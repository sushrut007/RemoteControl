#include "HostPage.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QTimer>
#include <QElapsedTimer>
#include <QPainter>
#include <QPainterPath>
#include <QClipboard>
#include <QApplication>
#include <QFrame>
#include <QSizePolicy>
#include <QResizeEvent>

// ── libqrencode (optional) ────────────────────────────────────────────────
// If libqrencode is available and linked, define HAVE_LIBQRENCODE=1
// in your CMakeLists.txt (target_compile_definitions).  Otherwise the
// QrCodeWidget falls back to displaying the URL as plain text.
#ifdef HAVE_LIBQRENCODE
#  include <qrencode.h>
#endif

// ===========================================================================
// QrCodeWidget
// ===========================================================================

QrCodeWidget::QrCodeWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(120, 120);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    setToolTip(tr("Scan to join room"));
}

QrCodeWidget::~QrCodeWidget() = default;

void QrCodeWidget::setUrl(const QString& url)
{
    if (m_url == url) { return; }
    m_url = url;
    regenerate();
    update();
}

void QrCodeWidget::regenerate()
{
    m_valid = false;
    m_qrImage = QImage();

    if (m_url.isEmpty()) { return; }

#ifdef HAVE_LIBQRENCODE
    QRcode* qr = QRcode_encodeString(m_url.toUtf8().constData(),
        0, QR_ECLEVEL_M,
        QR_MODE_8, 1);
    if (!qr) { return; }

    const int side = qr->width;
    m_qrImage = QImage(side, side, QImage::Format_Mono);
    m_qrImage.fill(1); // white background (0=black in Format_Mono)

    for (int y = 0; y < side; ++y) {
        for (int x = 0; x < side; ++x) {
            const bool dark = (qr->data[y * side + x] & 1) != 0;
            m_qrImage.setPixel(x, y, dark ? 0 : 1);
        }
    }
    QRcode_free(qr);
    m_valid = true;
#else
    // No libqrencode – m_valid stays false; paintEvent draws URL text.
    m_valid = false;
#endif
}

void QrCodeWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Card background
    p.fillRect(rect(), QColor(0xff, 0xff, 0xff));

    const int pad = 6;
    const QRect inner = rect().adjusted(pad, pad, -pad, -pad);

    if (m_valid && !m_qrImage.isNull()) {
        // Scale the 1-bit QR image to fill the inner rect with nearest-neighbour
        p.drawImage(inner,
            m_qrImage.scaled(inner.size(),
                Qt::KeepAspectRatio,
                Qt::FastTransformation));
    }
    else {
        // Fallback: show the URL as small text
        p.setPen(Qt::black);
        QFont f = p.font();
        f.setPointSize(7);
        p.setFont(f);
        p.drawText(inner, Qt::AlignCenter | Qt::TextWordWrap,
            m_url.isEmpty() ? tr("No URL") : m_url);
    }
}

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
    p.fillRect(rect(), QColor(0x05, 0x05, 0x15));
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
    // ── Root layout: [left info panel | right area(preview + status)] ──
    auto* rootLayout = new QHBoxLayout(this);
    rootLayout->setContentsMargins(12, 12, 12, 12);
    rootLayout->setSpacing(12);

    // ════════════════════════════════════════════════════════════════════
    // LEFT: Info panel
    // ════════════════════════════════════════════════════════════════════
    auto* infoPanel = new QWidget(this);
    infoPanel->setObjectName(QStringLiteral("InfoPanel"));
    infoPanel->setFixedWidth(280);

    auto* infoLayout = new QVBoxLayout(infoPanel);
    infoLayout->setContentsMargins(10, 10, 10, 10);
    infoLayout->setSpacing(8);

    // ── Room ID ──────────────────────────────────────────────────────
    infoLayout->addWidget(makeHeader(tr("Room ID"), infoPanel));

    auto* roomRow = new QHBoxLayout();
    roomRow->setSpacing(6);
    m_roomIdLabel = new QLabel(QStringLiteral("—"), infoPanel);
    m_roomIdLabel->setObjectName(QStringLiteral("RoomIdLabel"));
    m_roomIdLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_copyRoomIdBtn = new QPushButton(tr("Copy"), infoPanel);
    m_copyRoomIdBtn->setObjectName(QStringLiteral("SmallBtn"));
    m_copyRoomIdBtn->setFixedWidth(50);
    roomRow->addWidget(m_roomIdLabel, 1);
    roomRow->addWidget(m_copyRoomIdBtn);
    infoLayout->addLayout(roomRow);

    // ── Password ─────────────────────────────────────────────────────
    infoLayout->addWidget(makeHeader(tr("Password"), infoPanel));

    auto* passRow = new QHBoxLayout();
    passRow->setSpacing(6);
    m_passwordLabel = new QLabel(QStringLiteral("——————"), infoPanel);
    m_passwordLabel->setObjectName(QStringLiteral("PasswordLabel"));
    m_togglePassBtn = new QPushButton(tr("Show"), infoPanel);
    m_togglePassBtn->setObjectName(QStringLiteral("SmallBtn"));
    m_togglePassBtn->setFixedWidth(50);
    passRow->addWidget(m_passwordLabel, 1);
    passRow->addWidget(m_togglePassBtn);
    infoLayout->addLayout(passRow);

    infoLayout->addWidget(makeSep(infoPanel));

    // ── QR Code ──────────────────────────────────────────────────────
    infoLayout->addWidget(makeHeader(tr("Join via QR"), infoPanel));
    m_qrWidget = new QrCodeWidget(infoPanel);
    auto* qrRow = new QHBoxLayout();
    qrRow->addStretch();
    qrRow->addWidget(m_qrWidget);
    qrRow->addStretch();
    infoLayout->addLayout(qrRow);

    infoLayout->addWidget(makeSep(infoPanel));

    // ── Peer list ────────────────────────────────────────────────────
    infoLayout->addWidget(makeHeader(tr("Connected Viewers"), infoPanel));
    m_peerList = new QListWidget(infoPanel);
    m_peerList->setObjectName(QStringLiteral("PeerList"));
    m_peerList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_peerList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    infoLayout->addWidget(m_peerList, 1);

    rootLayout->addWidget(infoPanel);

    // ════════════════════════════════════════════════════════════════════
    // RIGHT: main area  (preview + control + status)
    // ════════════════════════════════════════════════════════════════════
    auto* rightLayout = new QVBoxLayout();
    rightLayout->setSpacing(8);

    // ── Preview thumbnail (fixed size, right-aligned) ─────────────
    m_preview = new PreviewWidget(this);
    auto* previewRow = new QHBoxLayout();
    previewRow->addStretch();
    previewRow->addWidget(m_preview);
    rightLayout->addLayout(previewRow);

    rightLayout->addStretch();

    // ── Control panel ────────────────────────────────────────────────
    auto* ctrlFrame = new QWidget(this);
    ctrlFrame->setObjectName(QStringLiteral("ControlPanel"));

    auto* ctrlLayout = new QHBoxLayout(ctrlFrame);
    ctrlLayout->setContentsMargins(10, 8, 10, 8);
    ctrlLayout->setSpacing(10);

    m_shareBtn = new QPushButton(tr("Share Screen"), ctrlFrame);
    m_shareBtn->setObjectName(QStringLiteral("ToggleBtn"));
    m_shareBtn->setCheckable(true);
    m_shareBtn->setFixedHeight(38);

    m_controlBtn = new QPushButton(tr("Allow Control"), ctrlFrame);
    m_controlBtn->setObjectName(QStringLiteral("ToggleBtn"));
    m_controlBtn->setCheckable(true);
    m_controlBtn->setFixedHeight(38);

    m_kickBtn = new QPushButton(tr("Kick Peer"), ctrlFrame);
    m_kickBtn->setObjectName(QStringLiteral("DangerBtn"));
    m_kickBtn->setEnabled(false);
    m_kickBtn->setFixedHeight(38);

    m_endBtn = new QPushButton(tr("End Session"), ctrlFrame);
    m_endBtn->setObjectName(QStringLiteral("EndBtn"));
    m_endBtn->setFixedHeight(38);

    ctrlLayout->addWidget(m_shareBtn, 1);
    ctrlLayout->addWidget(m_controlBtn, 1);
    ctrlLayout->addStretch();
    ctrlLayout->addWidget(m_kickBtn);
    ctrlLayout->addWidget(m_endBtn);

    rightLayout->addWidget(ctrlFrame);

    // ── Status bar ────────────────────────────────────────────────────
    auto* statusBar = new QWidget(this);
    statusBar->setObjectName(QStringLiteral("StatusBar"));
    statusBar->setFixedHeight(30);

    auto* statusLayout = new QHBoxLayout(statusBar);
    statusLayout->setContentsMargins(10, 0, 10, 0);
    statusLayout->setSpacing(14);

    m_statusDot = new QLabel(statusBar);
    m_statusDot->setFixedSize(10, 10);
    m_statusDot->setObjectName(QStringLiteral("StatusDotPaused"));

    m_statusText = new QLabel(tr("Paused"), statusBar);
    m_statusText->setObjectName(QStringLiteral("StatusText"));

    m_viewerCountLabel = new QLabel(tr("0 viewers"), statusBar);
    m_viewerCountLabel->setObjectName(QStringLiteral("StatusText"));

    m_uptimeLabel = new QLabel(tr("00:00:00"), statusBar);
    m_uptimeLabel->setObjectName(QStringLiteral("StatusText"));

    statusLayout->addWidget(m_statusDot);
    statusLayout->addWidget(m_statusText);
    statusLayout->addSpacing(6);
    statusLayout->addWidget(m_viewerCountLabel);
    statusLayout->addStretch();
    statusLayout->addWidget(m_uptimeLabel);

    rightLayout->addWidget(statusBar);

    rootLayout->addLayout(rightLayout, 1);

    // ════════════════════════════════════════════════════════════════════
    // Signals
    // ════════════════════════════════════════════════════════════════════
    QObject::connect(m_shareBtn, &QPushButton::clicked, this, &HostPage::onShareClicked);
    QObject::connect(m_controlBtn, &QPushButton::clicked, this, &HostPage::onControlClicked);
    QObject::connect(m_kickBtn, &QPushButton::clicked, this, &HostPage::onKickClicked);
    QObject::connect(m_endBtn, &QPushButton::clicked, this, &HostPage::onEndSessionClicked);
    QObject::connect(m_peerList, &QListWidget::itemSelectionChanged,
        this, &HostPage::onPeerSelectionChanged);
    QObject::connect(m_copyRoomIdBtn, &QPushButton::clicked, this, &HostPage::onCopyRoomId);
    QObject::connect(m_togglePassBtn, &QPushButton::clicked, this, &HostPage::onTogglePasswordVisible);

    // ════════════════════════════════════════════════════════════════════
    // Stylesheet
    // ════════════════════════════════════════════════════════════════════
    setStyleSheet(QStringLiteral(R"(
        HostPage {
            background: #1a1a2e;
        }
        #InfoPanel {
            background: #16213e;
            border-radius: 10px;
            border: 1px solid #2a2a5e;
        }
        #SectionHeader {
            color: #7070cc;
            font-size: 11px;
            font-weight: bold;
            text-transform: uppercase;
            letter-spacing: 1px;
        }
        #Separator {
            color: #2a2a5e;
        }
        #RoomIdLabel, #PasswordLabel {
            color: #e0e0ff;
            font-size: 13px;
            font-family: Consolas, monospace;
            background: #0f1630;
            border: 1px solid #2a2a5e;
            border-radius: 5px;
            padding: 4px 8px;
        }
        #PeerList {
            background: #0f1630;
            color: #ccccee;
            border: 1px solid #2a2a5e;
            border-radius: 6px;
            font-size: 12px;
        }
        #PeerList::item:selected {
            background: #2a2a6e;
            color: #ffffff;
        }
        #SmallBtn {
            background: #1e1e4e;
            color: #aaaaee;
            border: 1px solid #2a2a6e;
            border-radius: 5px;
            font-size: 11px;
            padding: 3px 6px;
        }
        #SmallBtn:hover  { background: #2a2a6e; color: #ffffff; }
        #SmallBtn:pressed { background: #111130; }
        #ControlPanel {
            background: #16213e;
            border-radius: 10px;
            border: 1px solid #2a2a5e;
        }
        #ToggleBtn {
            background: #1e1e4e;
            color: #aaaaee;
            border: 1px solid #2a2a6e;
            border-radius: 8px;
            font-size: 13px;
            padding: 0 16px;
        }
        #ToggleBtn:hover   { background: #2a2a6e; color: #ffffff; }
        #ToggleBtn:checked {
            background: #6c63ff;
            color: #ffffff;
            border-color: #6c63ff;
        }
        #ToggleBtn:checked:hover { background: #7d75ff; }
        #DangerBtn {
            background: #3a1530;
            color: #ff6688;
            border: 1px solid #7a2050;
            border-radius: 8px;
            font-size: 13px;
            padding: 0 16px;
        }
        #DangerBtn:hover   { background: #5a1f45; }
        #DangerBtn:pressed { background: #2a0f22; }
        #DangerBtn:disabled { background: #1e1e2e; color: #555566; border-color: #333355; }
        #EndBtn {
            background: #3a1010;
            color: #ff5555;
            border: 1px solid #7a2020;
            border-radius: 8px;
            font-size: 13px;
            font-weight: bold;
            padding: 0 16px;
        }
        #EndBtn:hover   { background: #5a1818; }
        #EndBtn:pressed { background: #280a0a; }
        #StatusBar {
            background: #0f1425;
            border-radius: 6px;
            border: 1px solid #1e1e4e;
        }
        #StatusDotLive {
            background: #44dd66;
            border-radius: 5px;
        }
        #StatusDotPaused {
            background: #888899;
            border-radius: 5px;
        }
        #StatusText {
            color: #9999bb;
            font-size: 12px;
        }
    )"));
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

    m_roomIdLabel->setText(roomId);

    // Update password display
    m_passwordVisible = false;
    m_passwordLabel->setText(QString(password.length(), QChar(0x2022))); // bullets
    m_togglePassBtn->setText(tr("Show"));

    // Build join URL and populate QR widget
    const QString url = serverUrl + QStringLiteral("/?room=") + roomId;
    m_qrWidget->setUrl(url);

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
    m_peerList->addItem(item);

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
    // Brief visual feedback
    m_copyRoomIdBtn->setText(tr("✓"));
    QTimer::singleShot(1200, m_copyRoomIdBtn, [this]() {
        m_copyRoomIdBtn->setText(tr("Copy"));
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
}

QString HostPage::selectedPeerId() const
{
    const auto items = m_peerList->selectedItems();
    if (items.isEmpty()) { return {}; }
    return items.first()->data(Qt::UserRole).toString();
}
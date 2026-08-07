#include "KickPeerModal.h"
#include "DarpanTheme.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QFrame>
#include <QDateTime>
#include <QPainter>
#include <QPainterPath>
#include <QGraphicsDropShadowEffect>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static QString formatDuration(const QDateTime& joinTime)
{
    const qint64 secs = joinTime.secsTo(QDateTime::currentDateTime());
    if (secs < 60) { return QObject::tr("%1s").arg(secs); }
    if (secs < 3600) { return QObject::tr("%1m %2s").arg(secs / 60).arg(secs % 60); }
    return QObject::tr("%1h %2m").arg(secs / 3600).arg((secs % 3600) / 60);
}

static QColor appTypeColor(const QString& appType)
{
    if (appType == QLatin1String("host")) { return QColor(0xff, 0xaa, 0x33); }
    if (appType == QLatin1String("controller")) { return QColor(0x6c, 0x63, 0xff); }
    return QColor(0x44, 0xdd, 0x88); // viewer
}

// ===========================================================================
// KickPeerModal
// ===========================================================================

KickPeerModal::KickPeerModal(const PeerInfo& peer, QWidget* parent)
    : QDialog(parent, Qt::Dialog | Qt::FramelessWindowHint)
    , m_peerId(peer.id)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedSize(400, 280);

    auto* shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(28);
    shadow->setOffset(0, 5);
    shadow->setColor(QColor(0, 0, 0, 130));
    setGraphicsEffect(shadow);

    buildUi(peer);
}

void KickPeerModal::buildUi(const PeerInfo& peer)
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    auto* card = new QFrame(this);
    card->setObjectName(QStringLiteral("KickCard"));
    auto* root = new QVBoxLayout(card);
    root->setContentsMargins(20, 20, 20, 20);
    root->setSpacing(12);

    // ── Title ─────────────────────────────────────────────────────────────
    auto* title = new QLabel(tr("Kick Peer"), card);
    title->setObjectName(QStringLiteral("KickTitle"));
    root->addWidget(title);

    // ── Peer info card ────────────────────────────────────────────────────
    auto* infoCard = new QFrame(card);
    infoCard->setObjectName(QStringLiteral("KickInfoCard"));
    auto* cardLayout = new QFormLayout(infoCard);
    cardLayout->setContentsMargins(12, 10, 12, 10);
    cardLayout->setSpacing(7);
    cardLayout->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    auto makeVal = [&](const QString& text) {   // auto* → auto
        auto* lbl = new QLabel(text, infoCard);
        lbl->setObjectName(QStringLiteral("KickInfoVal"));
        return lbl;
        };
    auto makeLbl = [&](const QString& text) {   // auto* → auto
        auto* lbl = new QLabel(text, infoCard);
        lbl->setObjectName(QStringLiteral("KickInfoKey"));
        return lbl;
        };

    const QString displayId = peer.id.length() > 24    // peerId → id
        ? peer.id.left(21) + QStringLiteral("…")
        : peer.id;
    auto* idVal = makeVal(displayId);
    idVal->setToolTip(peer.id);                        // peerId → id
    cardLayout->addRow(makeLbl(tr("Peer ID")), idVal);

    // App type with coloured dot
    auto* typeRow = new QHBoxLayout();
    auto* dot = new QLabel(infoCard);
    dot->setFixedSize(10, 10);
    const QColor tc = appTypeColor(peer.appType);
    dot->setStyleSheet(
        QStringLiteral("background:%1; border-radius:5px;").arg(tc.name()));
    typeRow->addWidget(dot);
    typeRow->addSpacing(4);
    typeRow->addWidget(makeVal(peer.appType));
    typeRow->addStretch();
    cardLayout->addRow(makeLbl(tr("App Type")), typeRow);

    cardLayout->addRow(makeLbl(tr("Join Time")),
        makeVal(peer.joinedAt.toString(QStringLiteral("hh:mm:ss"))));  // joinTime → joinedAt
    cardLayout->addRow(makeLbl(tr("Duration")),
        makeVal(formatDuration(peer.joinedAt)));                        // joinTime → joinedAt

    root->addWidget(infoCard);

    // ── Confirmation message ──────────────────────────────────────────────
    auto* msg = new QLabel(
        tr("Are you sure you want to kick this peer?"), card);
    msg->setObjectName(QStringLiteral("KickMsg"));
    msg->setWordWrap(true);
    root->addWidget(msg);

    // ── Reason field ──────────────────────────────────────────────────────
    auto* reasonRow = new QHBoxLayout();
    auto* reasonLbl = new QLabel(tr("Reason:"), card);
    reasonLbl->setObjectName(QStringLiteral("KickInfoKey"));
    m_reasonEdit = new QLineEdit(card);
    m_reasonEdit->setObjectName(QStringLiteral("KickReasonEdit"));
    m_reasonEdit->setPlaceholderText(tr("Optional — sent to peer"));
    m_reasonEdit->setMaxLength(128);
    reasonRow->addWidget(reasonLbl);
    reasonRow->addWidget(m_reasonEdit, 1);
    root->addLayout(reasonRow);

    root->addStretch();

    // ── Buttons ───────────────────────────────────────────────────────────
    auto* btnRow = new QHBoxLayout();
    btnRow->addStretch();

    m_cancelBtn = new QPushButton(tr("Cancel"), card);
    m_cancelBtn->setObjectName(QStringLiteral("KickCancelBtn"));
    m_cancelBtn->setFixedHeight(34);

    m_kickBtn = new QPushButton(tr("Kick"), card);
    m_kickBtn->setObjectName(QStringLiteral("KickConfirmBtn"));
    m_kickBtn->setFixedHeight(34);
    m_kickBtn->setDefault(true);

    btnRow->addWidget(m_cancelBtn);
    btnRow->addSpacing(8);
    btnRow->addWidget(m_kickBtn);
    root->addLayout(btnRow);

    outer->addWidget(card);

    QObject::connect(m_kickBtn, &QPushButton::clicked, this, &KickPeerModal::onKickClicked);
    QObject::connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    QObject::connect(m_reasonEdit, &QLineEdit::returnPressed,
        this, &KickPeerModal::onKickClicked);
}

void KickPeerModal::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    path.addRoundedRect(rect(), 12, 12);
    p.fillPath(path, QColor(0x16, 0x1B, 0x22));
}

void KickPeerModal::onKickClicked()
{
    emit kickConfirmed(m_peerId, m_reasonEdit->text().trimmed());
    accept();
}
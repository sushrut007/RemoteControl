#include "PeerListWidget.h"
#include "KickPeerModal.h"

#include <QListView>
#include <QStandardItemModel>
#include <QStandardItem>
#include <QPainter>
#include <QPainterPath>
#include <QStyleOptionViewItem>
#include <QFontMetrics>
#include <QMenu>
#include <QAction>
#include <QDateTime>
#include <QVBoxLayout>
#include <QVariant>

// ---------------------------------------------------------------------------
// Custom roles stored on each QStandardItem
// ---------------------------------------------------------------------------
static constexpr int RolePeerId = Qt::UserRole + 1;
static constexpr int RoleAppType = Qt::UserRole + 2;
static constexpr int RoleJoinTime = Qt::UserRole + 3;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static QString formatJoinDuration(const QDateTime& joinTime)
{
    const qint64 secs = joinTime.secsTo(QDateTime::currentDateTime());
    if (secs < 60) { return QObject::tr("%1s ago").arg(secs); }
    if (secs < 3600) { return QObject::tr("%1m ago").arg(secs / 60); }
    return QObject::tr("%1h ago").arg(secs / 3600);
}

// ===========================================================================
// PeerItemDelegate
// ===========================================================================

PeerItemDelegate::PeerItemDelegate(QObject* parent)
    : QStyledItemDelegate(parent)
{
}

QColor PeerItemDelegate::dotColor(const QString& appType)
{
    if (appType == QLatin1String("host")) { return QColor(0xff, 0xaa, 0x33); }
    if (appType == QLatin1String("controller")) { return QColor(0x6c, 0x63, 0xff); }
    return QColor(0x44, 0xdd, 0x88); // viewer
}

QSize PeerItemDelegate::sizeHint(const QStyleOptionViewItem&,
    const QModelIndex&) const
{
    return QSize(0, 52);
}

void PeerItemDelegate::paint(QPainter* painter,
    const QStyleOptionViewItem& option,
    const QModelIndex& index) const
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    const QRect r = option.rect;
    const bool selected = option.state & QStyle::State_Selected;
    const bool hovered = option.state & QStyle::State_MouseOver;

    // ── Row background ────────────────────────────────────────────────────
    const QColor bg = selected ? QColor(0x2a, 0x2a, 0x6e)
        : hovered ? QColor(0x1e, 0x1e, 0x4a)
        : QColor(0x10, 0x14, 0x28);
    painter->fillRect(r, bg);

    // Bottom divider
    painter->setPen(QColor(0x2a, 0x2a, 0x5e));
    painter->drawLine(r.left() + 44, r.bottom(), r.right(), r.bottom());

    const QString peerId = index.data(RolePeerId).toString();
    const QString appType = index.data(RoleAppType).toString();
    const QDateTime joinTime = index.data(RoleJoinTime).toDateTime();

    // ── Coloured dot (left margin) ────────────────────────────────────────
    const int dotR = 6;
    const QPoint dotCenter(r.left() + 20, r.center().y());
    painter->setPen(Qt::NoPen);
    painter->setBrush(dotColor(appType));
    painter->drawEllipse(dotCenter, dotR, dotR);

    // ── Peer ID (primary text) ────────────────────────────────────────────
    const int textX = r.left() + 44;
    const int badgeW = 60;
    const int textMaxW = r.width() - 44 - badgeW - 12;

    QFont primaryFont = option.font;
    primaryFont.setPointSize(11);
    primaryFont.setBold(false);
    painter->setFont(primaryFont);
    painter->setPen(QColor(0xe0, 0xe0, 0xff));

    const QFontMetrics primaryFm(primaryFont);
    const QString displayId = primaryFm.elidedText(peerId, Qt::ElideRight, textMaxW);
    painter->drawText(textX, r.top() + 18, displayId);

    // ── Join time (secondary text) ────────────────────────────────────────
    QFont secondaryFont = option.font;
    secondaryFont.setPointSize(9);
    painter->setFont(secondaryFont);
    painter->setPen(QColor(0x77, 0x77, 0xaa));
    painter->drawText(textX, r.top() + 36, formatJoinDuration(joinTime));

    // ── App type badge (right side) ───────────────────────────────────────
    const QColor badgeBg = dotColor(appType).darker(160);
    const QColor badgeFg = dotColor(appType).lighter(140);
    const QRect  badgeRect(r.right() - badgeW - 8,
        r.center().y() - 10,
        badgeW, 20);

    painter->setPen(Qt::NoPen);
    painter->setBrush(badgeBg);
    painter->drawRoundedRect(badgeRect, 4, 4);

    QFont badgeFont = option.font;
    badgeFont.setPointSize(9);
    badgeFont.setBold(true);
    painter->setFont(badgeFont);
    painter->setPen(badgeFg);
    painter->drawText(badgeRect, Qt::AlignCenter, appType.toUpper());

    painter->restore();
}

// ===========================================================================
// PeerListWidget
// ===========================================================================

PeerListWidget::PeerListWidget(QWidget* parent)
    : QWidget(parent)
    , m_model(new QStandardItemModel(this))
    , m_view(new QListView(this))
{
    m_view->setModel(m_model);
    m_view->setItemDelegate(new PeerItemDelegate(this));
    m_view->setSelectionMode(QAbstractItemView::SingleSelection);
    m_view->setMouseTracking(true);
    m_view->setFrameShape(QFrame::NoFrame);
    m_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_view->setUniformItemSizes(true);

    m_view->setStyleSheet(QStringLiteral(
        "QListView { background: #10141e; outline: none; }"
        "QListView::item { border: none; }"
        "QScrollBar:vertical { background:#10141e; width:6px; border:none; }"
        "QScrollBar::handle:vertical { background:#2a2a5e; border-radius:3px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_view);

    QObject::connect(m_view, &QListView::customContextMenuRequested,
        this, &PeerListWidget::onCustomContextMenu);
    QObject::connect(m_view, &QListView::doubleClicked,
        this, &PeerListWidget::onDoubleClicked);
}

// ---------------------------------------------------------------------------
// Public
// ---------------------------------------------------------------------------

void PeerListWidget::updatePeers(const QList<PeerInfo>& peers)
{
    QHash<QString, int> existing;
    for (int i = 0; i < m_model->rowCount(); ++i) {
        existing.insert(m_model->item(i)->data(RolePeerId).toString(), i);
    }

    QSet<QString> incoming;
    for (const PeerInfo& p : peers) { incoming.insert(p.id); }          // peerId → id

    for (int i = m_model->rowCount() - 1; i >= 0; --i) {
        const QString id = m_model->item(i)->data(RolePeerId).toString();
        if (!incoming.contains(id)) {
            m_model->removeRow(i);
        }
    }

    existing.clear();
    for (int i = 0; i < m_model->rowCount(); ++i) {
        existing.insert(m_model->item(i)->data(RolePeerId).toString(), i);
    }

    for (const PeerInfo& p : peers) {
        if (existing.contains(p.id)) {                                   // peerId → id
            QStandardItem* item = m_model->item(existing.value(p.id));  // peerId → id
            item->setData(p.appType, RoleAppType);
            item->setData(p.joinedAt, RoleJoinTime);                   // joinTime → joinedAt
        }
        else {
            m_model->appendRow(makeItem(p));
        }
    }
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

QStandardItem* PeerListWidget::makeItem(const PeerInfo& peer) const
{
    auto* item = new QStandardItem();
    item->setData(peer.id, RolePeerId);                           // peerId → id
    item->setData(peer.appType, RoleAppType);
    item->setData(peer.joinedAt, RoleJoinTime);                         // joinTime → joinedAt
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    return item;
}

QString PeerListWidget::peerIdAt(const QModelIndex& index) const
{
    if (!index.isValid()) { return {}; }
    return m_model->itemFromIndex(index)->data(RolePeerId).toString();
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

void PeerListWidget::onCustomContextMenu(const QPoint& pos)
{
    const QModelIndex idx = m_view->indexAt(pos);
    const QString peerId = peerIdAt(idx);
    if (peerId.isEmpty()) { return; }

    QMenu menu(this);
    menu.setStyleSheet(QStringLiteral(
        "QMenu { background:#16213e; color:#dcdcf0; border:1px solid #2a2a5e; "
        "        border-radius:6px; padding:4px 0; }"
        "QMenu::item { padding:7px 24px; font-size:13px; }"
        "QMenu::item:selected { background:#2a2a6e; }"
        "QMenu::separator { height:1px; background:#2a2a5e; margin:3px 10px; }"));

    QAction* detailsAct = menu.addAction(tr("View Details"));
    menu.addSeparator();
    QAction* kickAct = menu.addAction(tr("Kick Peer"));

    QAction* chosen = menu.exec(m_view->viewport()->mapToGlobal(pos));
    if (chosen == detailsAct) { emit detailsRequested(peerId); }
    else if (chosen == kickAct) { emit kickRequested(peerId); }
}

void PeerListWidget::onDoubleClicked(const QModelIndex& index)
{
    const QString peerId = peerIdAt(index);
    if (peerId.isEmpty()) { return; }

    PeerInfo peer;
    QStandardItem* item = m_model->itemFromIndex(index);
    peer.id = peerId;                                              // peerId → id
    peer.appType = item->data(RoleAppType).toString();
    peer.joinedAt = item->data(RoleJoinTime).toDateTime();               // joinTime → joinedAt

    auto* modal = new KickPeerModal(peer, this);
    modal->setAttribute(Qt::WA_DeleteOnClose);
    QObject::connect(modal, &KickPeerModal::kickConfirmed,
        this, [this](const QString& id, const QString&) {
            emit kickRequested(id);
        });
    modal->exec();
}
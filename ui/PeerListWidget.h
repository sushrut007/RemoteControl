#pragma once

#include <QWidget>
#include <QList>
#include <QStyledItemDelegate>
#include "HostPage.h"  // PeerInfo

QT_FORWARD_DECLARE_CLASS(QListView)
QT_FORWARD_DECLARE_CLASS(QStandardItemModel)
QT_FORWARD_DECLARE_CLASS(QStandardItem)

// ---------------------------------------------------------------------------
// PeerItemDelegate
// ---------------------------------------------------------------------------
class PeerItemDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit PeerItemDelegate(QObject* parent = nullptr);

    void paint(QPainter* painter,
        const QStyleOptionViewItem& option,
        const QModelIndex& index) const override;

    QSize sizeHint(const QStyleOptionViewItem& option,
        const QModelIndex& index) const override;

private:
    static QColor dotColor(const QString& appType);
};

// ---------------------------------------------------------------------------
// PeerListWidget
// ---------------------------------------------------------------------------
class PeerListWidget : public QWidget
{
    Q_OBJECT
public:
    explicit PeerListWidget(QWidget* parent = nullptr);

    void updatePeers(const QList<PeerInfo>& peers);

signals:
    void kickRequested(const QString& peerId);
    void detailsRequested(const QString& peerId);

private slots:
    void onCustomContextMenu(const QPoint& pos);
    void onDoubleClicked(const QModelIndex& index);

private:
    QStandardItem* makeItem(const PeerInfo& peer) const;
    QString        peerIdAt(const QModelIndex& index) const;

    QListView* m_view{ nullptr };
    QStandardItemModel* m_model{ nullptr };
};
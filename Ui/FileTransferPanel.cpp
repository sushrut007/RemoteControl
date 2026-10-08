#include "FileTransferPanel.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QFrame>
#include <QColor>
#include <QPalette>
#include <QVBoxLayout>

namespace {

QString formatBytes(quint64 bytes)
{
    if (bytes >= 1024 * 1024) {
        return QStringLiteral("%1 MB").arg(double(bytes) / (1024.0 * 1024.0), 0, 'f', 1);
    }
    if (bytes >= 1024) {
        return QStringLiteral("%1 KB").arg(double(bytes) / 1024.0, 0, 'f', 1);
    }
    return QStringLiteral("%1 B").arg(bytes);
}

} // namespace

FileTransferPanel::FileTransferPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("FileTransferPanel"));

    m_rootLayout = new QVBoxLayout(this);
    m_rootLayout->setContentsMargins(0, 0, 0, 0);
    m_rootLayout->setSpacing(12);

    auto *activeTitle = new QLabel(tr("Active transfers"));
    activeTitle->setObjectName(QStringLiteral("SectionTitle"));
    m_activeList = new QListWidget;
    m_activeList->setMinimumHeight(120);
    m_activeList->setFrameShape(QFrame::NoFrame);

    m_historyTitle = new QLabel(tr("History"));
    m_historyTitle->setObjectName(QStringLiteral("SectionTitle"));
    m_historyList = new QListWidget;
    m_historyList->setMinimumHeight(100);
    m_historyList->setFrameShape(QFrame::NoFrame);

    m_rootLayout->addWidget(activeTitle);
    m_rootLayout->addWidget(m_activeList, 1);
    m_rootLayout->addWidget(m_historyTitle);
    m_rootLayout->addWidget(m_historyList, 1);
}

FileTransferPanel::RowWidgets *FileTransferPanel::ensureRow(uint32_t id)
{
    if (m_rows.contains(id)) {
        return &m_rows[id];
    }

    RowWidgets row;
    row.row = new QWidget;
    row.row->setObjectName(QStringLiteral("FileTransferRow"));
    auto *lay = new QHBoxLayout(row.row);
    lay->setContentsMargins(8, 6, 8, 6);
    lay->setSpacing(10);

    row.nameLabel = new QLabel;
    row.nameLabel->setObjectName(QStringLiteral("FileTransferRowLabel"));
    row.nameLabel->setMinimumWidth(140);
    row.bar = new QProgressBar;
    row.bar->setObjectName(QStringLiteral("FileTransferProgressBar"));
    row.bar->setRange(0, 1000);
    row.bar->setValue(0);
    row.bar->setTextVisible(true);
    row.bar->setFormat(QStringLiteral("%p%"));
    {
        QPalette barPal = row.bar->palette();
        barPal.setColor(QPalette::WindowText, QColor(QStringLiteral("#e7ecf3")));
        barPal.setColor(QPalette::Text, QColor(QStringLiteral("#e7ecf3")));
        row.bar->setPalette(barPal);
    }
    row.statusLabel = new QLabel(tr("Starting…"));
    row.statusLabel->setObjectName(QStringLiteral("FileTransferRowLabel"));
    row.cancelBtn = new QPushButton(tr("Cancel"));
    row.cancelBtn->setObjectName(QStringLiteral("SecondaryButton"));

    connect(row.cancelBtn, &QPushButton::clicked, this, [this, id]() { emit cancelRequested(id); });

    lay->addWidget(row.nameLabel, 2);
    lay->addWidget(row.bar, 4);
    lay->addWidget(row.statusLabel, 2);
    lay->addWidget(row.cancelBtn);

    row.item = new QListWidgetItem(m_activeList);
    row.item->setSizeHint(row.row->sizeHint());
    m_activeList->addItem(row.item);
    m_activeList->setItemWidget(row.item, row.row);

    m_rows.insert(id, row);
    return &m_rows[id];
}

void FileTransferPanel::addTransfer(uint32_t id, const QString &fileName, quint64 totalBytes, bool outgoing)
{
    RowWidgets *row = ensureRow(id);
    row->timer.start();
    const QString dir = outgoing ? tr("Sending") : tr("Receiving");
    row->nameLabel->setText(QStringLiteral("%1 · %2").arg(dir, fileName));
    row->bar->setMaximum(int(qMax<quint64>(1, totalBytes)));
    row->bar->setValue(0);
    row->statusLabel->setText(QStringLiteral("0 / %1").arg(formatBytes(totalBytes)));
}

void FileTransferPanel::updateProgress(uint32_t id, quint64 sent, quint64 total)
{
    RowWidgets *row = ensureRow(id);
    const quint64 t = qMax<quint64>(1, total);
    row->bar->setMaximum(int(t));
    row->bar->setValue(int(qMin(sent, t)));
    const int pct = int(double(sent) / double(t) * 100.0);
    row->bar->setFormat(QStringLiteral("%p%"));

    qint64 ms = row->timer.elapsed();
    QString speedStr;
    if (ms > 0 && sent > 0) {
        double bytesPerSec = double(sent) / (double(ms) / 1000.0);
        speedStr = QStringLiteral(" | %1/s").arg(formatBytes(quint64(bytesPerSec)));
    }
    QString timeStr;
    if (ms > 0) {
        timeStr = QStringLiteral(" | %1s").arg(ms / 1000);
    }

    row->statusLabel->setText(QStringLiteral("%1 / %2 (%3%)%4%5")
                                  .arg(formatBytes(sent), formatBytes(total), QString::number(pct), speedStr, timeStr));
}

void FileTransferPanel::finishTransfer(uint32_t id, bool success, const QString &message)
{
    if (!m_rows.contains(id)) {
        appendHistoryLine(message);
        return;
    }

    RowWidgets row = m_rows.take(id);
    const QString name = row.nameLabel->text();
    if (row.item) {
        const int idx = m_activeList->row(row.item);
        m_activeList->takeItem(idx);
    }
    delete row.row;

    appendHistoryLine(QStringLiteral("%1 — %2").arg(name, success ? tr("Done: %1").arg(message)
                                                                 : tr("Failed: %1").arg(message)));
}

void FileTransferPanel::appendHistoryLine(const QString &line)
{
    auto *item = new QListWidgetItem(line);
    m_historyList->insertItem(0, item);
    while (m_historyList->count() > 50) {
        delete m_historyList->takeItem(m_historyList->count() - 1);
    }
}

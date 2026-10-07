#pragma once

#include <QHash>
#include <QWidget>

class QLabel;
class QProgressBar;
class QPushButton;
class QVBoxLayout;
class QListWidget;
class QListWidgetItem;

class FileTransferPanel : public QWidget
{
    Q_OBJECT

public:
    explicit FileTransferPanel(QWidget *parent = nullptr);

    void addTransfer(uint32_t id, const QString &fileName, quint64 totalBytes, bool outgoing);
    void updateProgress(uint32_t id, quint64 sent, quint64 total);
    void finishTransfer(uint32_t id, bool success, const QString &message);

signals:
    void cancelRequested(uint32_t transferId);

private:
    struct RowWidgets
    {
        QListWidgetItem *item = nullptr;
        QWidget *row = nullptr;
        QLabel *nameLabel = nullptr;
        QProgressBar *bar = nullptr;
        QLabel *statusLabel = nullptr;
        QPushButton *cancelBtn = nullptr;
    };

    RowWidgets *ensureRow(uint32_t id);
    void appendHistoryLine(const QString &line);

    QVBoxLayout *m_rootLayout = nullptr;
    QListWidget *m_activeList = nullptr;
    QListWidget *m_historyList = nullptr;
    QLabel *m_historyTitle = nullptr;
    QHash<uint32_t, RowWidgets> m_rows;
};

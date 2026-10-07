#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <functional>

namespace rtc {
struct DataChannel;
}

namespace darpan::webrtc {

class FileTransferManager : public QObject
{
    Q_OBJECT

public:
    explicit FileTransferManager(QObject *parent = nullptr);

    void setSendBinary(std::function<void(const QByteArray &)> sender);

    void sendFile(const QString &path);
    void cancelTransfer(uint32_t transferId);

signals:
    void incomingOffer(uint32_t transferId, const QString &fileName, quint64 size, const QByteArray &sha256);
    void transferStarted(uint32_t transferId, const QString &fileName, quint64 size, bool outgoing);
    void transferProgress(uint32_t transferId, quint64 sent, quint64 total);
    void transferFinished(uint32_t transferId, bool success, const QString &message);

public slots:
    void acceptIncoming(uint32_t transferId, const QString &savePath);
    void rejectIncoming(uint32_t transferId);

    void handleBinary(const QByteArray &payload);

private:
    struct ActiveSend;
    struct ActiveRecv;

    void sendChunk(ActiveSend *send);
    void pumpSend(uint32_t transferId);

    std::function<void(const QByteArray &)> m_send;
    QHash<uint32_t, ActiveSend *> m_sends;
    QHash<uint32_t, ActiveRecv *> m_recvs;
    uint32_t m_nextId = 1;
};

} // namespace darpan::webrtc

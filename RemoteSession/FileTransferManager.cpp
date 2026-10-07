#include "FileTransferManager.h"

#include "ControlProtocol.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QTimer>

namespace darpan::webrtc {

namespace {

constexpr int kChunkSize = 32 * 1024;

QByteArray packHeader(uint8_t type)
{
    QByteArray h;
    h.append(char(protocol::kVersion));
    h.append(char(type));
    return h;
}

void appendU32(QByteArray &b, uint32_t v)
{
    b.append(char(v & 0xff));
    b.append(char((v >> 8) & 0xff));
    b.append(char((v >> 16) & 0xff));
    b.append(char((v >> 24) & 0xff));
}

void appendU64(QByteArray &b, quint64 v)
{
    for (int i = 0; i < 8; ++i) {
        b.append(char((v >> (8 * i)) & 0xff));
    }
}

uint32_t readU32(const QByteArray &b, int o)
{
    return uint32_t(uint8_t(b[o])) | (uint32_t(uint8_t(b[o + 1])) << 8)
           | (uint32_t(uint8_t(b[o + 2])) << 16) | (uint32_t(uint8_t(b[o + 3])) << 24);
}

quint64 readU64(const QByteArray &b, int o)
{
    quint64 v = 0;
    for (int i = 0; i < 8; ++i) {
        v |= quint64(uint8_t(b[o + i])) << (8 * i);
    }
    return v;
}

} // namespace

struct FileTransferManager::ActiveSend
{
    uint32_t id = 0;
    QFile file;
    quint64 size = 0;
    quint64 offset = 0;
    QByteArray sha256;
};

struct FileTransferManager::ActiveRecv
{
    uint32_t id = 0;
    QFile file;
    quint64 size = 0;
    quint64 received = 0;
    QByteArray expectedSha256;
    QCryptographicHash hasher{QCryptographicHash::Sha256};
};

FileTransferManager::FileTransferManager(QObject *parent)
    : QObject(parent)
{
}

void FileTransferManager::setSendBinary(std::function<void(const QByteArray &)> sender)
{
    m_send = std::move(sender);
}

void FileTransferManager::sendFile(const QString &path)
{
    if (!m_send) {
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        emit transferFinished(0, false, QStringLiteral("Cannot open file"));
        return;
    }

    auto *send = new ActiveSend;
    send->id = m_nextId++;
    send->file.setFileName(path);
    send->file.open(QIODevice::ReadOnly);
    send->size = send->file.size();
    send->sha256 = QCryptographicHash::hash(send->file.readAll(), QCryptographicHash::Sha256);
    send->file.seek(0);
    m_sends.insert(send->id, send);

    const QByteArray name = QFileInfo(path).fileName().toUtf8();
    QByteArray offer = packHeader(protocol::kFileOffer);
    appendU32(offer, send->id);
    appendU64(offer, send->size);
    offer.append(char(name.size()));
    offer.append(name);
    offer.append(send->sha256);
    m_send(offer);
    emit transferStarted(send->id, QFileInfo(path).fileName(), send->size, true);
}

void FileTransferManager::acceptIncoming(uint32_t transferId, const QString &savePath)
{
    auto *recv = m_recvs.value(transferId, nullptr);
    if (!recv) {
        return;
    }
    recv->file.setFileName(savePath);
    recv->file.open(QIODevice::WriteOnly);
    QByteArray accept = packHeader(protocol::kFileAccept);
    appendU32(accept, transferId);
    m_send(accept);
}

void FileTransferManager::rejectIncoming(uint32_t transferId)
{
    m_recvs.remove(transferId);
    QByteArray reject = packHeader(protocol::kFileReject);
    appendU32(reject, transferId);
    if (m_send) {
        m_send(reject);
    }
}

void FileTransferManager::cancelTransfer(uint32_t transferId)
{
    if (m_sends.contains(transferId)) {
        delete m_sends.take(transferId);
    }
    if (m_recvs.contains(transferId)) {
        delete m_recvs.take(transferId);
    }
    QByteArray cancel = packHeader(protocol::kFileCancel);
    appendU32(cancel, transferId);
    if (m_send) {
        m_send(cancel);
    }
}

void FileTransferManager::pumpSend(uint32_t transferId)
{
    ActiveSend *send = m_sends.value(transferId, nullptr);
    if (!send || !m_send) {
        return;
    }

    if (send->offset >= send->size) {
        QByteArray done = packHeader(protocol::kFileComplete);
        appendU32(done, send->id);
        done.append(send->sha256);
        m_send(done);
        emit transferFinished(send->id, true, QStringLiteral("Complete"));
        delete m_sends.take(send->id);
        return;
    }

    QByteArray chunk = send->file.read(kChunkSize);
    if (chunk.isEmpty() && send->offset < send->size) {
        emit transferFinished(send->id, false, QStringLiteral("Read error"));
        delete m_sends.take(send->id);
        return;
    }
    if (chunk.isEmpty()) {
        pumpSend(transferId);
        return;
    }

    QByteArray msg = packHeader(protocol::kFileChunk);
    appendU32(msg, send->id);
    appendU64(msg, send->offset);
    appendU32(msg, uint32_t(chunk.size()));
    msg.append(chunk);
    m_send(msg);
    send->offset += chunk.size();
    emit transferProgress(send->id, send->offset, send->size);

    if (send->offset >= send->size) {
        QTimer::singleShot(0, this, [this, transferId]() { pumpSend(transferId); });
    } else {
        QTimer::singleShot(1, this, [this, transferId]() { pumpSend(transferId); });
    }
}

void FileTransferManager::sendChunk(ActiveSend *send)
{
    if (!send) {
        return;
    }
    pumpSend(send->id);
}

void FileTransferManager::handleBinary(const QByteArray &payload)
{
    if (payload.size() < 2 || uint8_t(payload[0]) != protocol::kVersion) {
        return;
    }
    const uint8_t type = uint8_t(payload[1]);
    const QByteArray body = payload.mid(2);

    if (type == protocol::kFileOffer && body.size() >= 13) {
        const uint32_t id = readU32(body, 0);
        const quint64 size = readU64(body, 4);
        const int nameLen = int(uint8_t(body[12]));
        if (body.size() < 13 + nameLen + 32) {
            return;
        }
        const QString name = QString::fromUtf8(body.mid(13, nameLen));
        const QByteArray sha = body.mid(13 + nameLen, 32);
        auto *recv = new ActiveRecv;
        recv->id = id;
        recv->size = size;
        recv->expectedSha256 = sha;
        m_recvs.insert(id, recv);
        emit incomingOffer(id, name, size, sha);
        emit transferStarted(id, name, size, false);
        return;
    }

    if (type == protocol::kFileAccept && body.size() >= 4) {
        const uint32_t id = readU32(body, 0);
        sendChunk(m_sends.value(id));
        return;
    }

    if (type == protocol::kFileChunk && body.size() >= 16) {
        const uint32_t id = readU32(body, 0);
        const quint64 offset = readU64(body, 4);
        const uint32_t len = readU32(body, 12);
        if (body.size() < 16 + int(len)) {
            return;
        }
        const QByteArray data = body.mid(16, int(len));
        auto *recv = m_recvs.value(id, nullptr);
        if (!recv || !recv->file.isOpen()) {
            return;
        }
        recv->file.seek(qint64(offset));
        recv->file.write(data);
        recv->received = offset + len;
        recv->hasher.addData(data);
        emit transferProgress(id, recv->received, recv->size);
        return;
    }

    if (type == protocol::kFileComplete && body.size() >= 36) {
        const uint32_t id = readU32(body, 0);
        const QByteArray sha = body.mid(4, 32);
        auto *recv = m_recvs.take(id);
        if (!recv) {
            return;
        }
        const bool ok = sha == recv->hasher.result() || recv->expectedSha256.isEmpty()
                        || sha == recv->expectedSha256;
        recv->file.close();
        emit transferFinished(id, ok, ok ? QStringLiteral("Verified") : QStringLiteral("Checksum mismatch"));
        delete recv;
    }
}

} // namespace darpan::webrtc

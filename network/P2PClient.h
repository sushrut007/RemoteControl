#pragma once

#include <QObject>
#include <QString>
#include <QByteArray>
#include <QImage>
#include <memory>
#include <string>
#include <functional>
#include <vector>

#include <nlohmann/json.hpp>

namespace rtc {
    class PeerConnection;
    class Track;
    class DataChannel;
    class H264RtpPacketizer;
}

class P2PClient : public QObject
{
    Q_OBJECT

public:
    explicit P2PClient(QObject* parent = nullptr);
    ~P2PClient() override;

    P2PClient(const P2PClient&) = delete;
    P2PClient& operator=(const P2PClient&) = delete;

    void init(bool isHost, const QString& stunServer,
              const QString& turnServer = QString(),
              const QString& turnUser = QString(),
              const QString& turnPass = QString());

    void close();
    bool isConnected() const;

    void createOffer();
    void setRemoteDescription(const QString& sdp, const QString& type);
    void addRemoteCandidate(const QString& candidate, const QString& mid);

    void sendVideoFrame(const QByteArray& naluData, bool isKeyframe);
    void sendControlMessage(const nlohmann::json& message);

signals:
    void localDescriptionGenerated(const QString& sdp, const QString& type);
    void localCandidateGenerated(const QString& candidate, const QString& mid);

    void connected();
    void disconnected();
    void connectionFailed(const QString& reason);

    void videoFrameReceived(const QByteArray& naluData, bool isKeyframe);
    void controlMessageReceived(const nlohmann::json& message);

private:
    void setupPeerConnectionCallbacks();

    std::shared_ptr<rtc::PeerConnection> m_pc;
    std::shared_ptr<rtc::Track> m_videoTrack;
    std::shared_ptr<rtc::DataChannel> m_dataChannel;
    std::shared_ptr<rtc::H264RtpPacketizer> m_packetizer;

    bool m_isHost{ false };
    bool m_connected{ false };
    uint32_t m_ssrc{ 1 };
    uint32_t m_rtpTimestamp{ 0 }; // 90 kHz RTP clock, incremented per frame
};

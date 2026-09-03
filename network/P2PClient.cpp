#include "P2PClient.h"
#include <rtc/rtc.hpp>

#include <QDebug>
#include <QMetaObject>

P2PClient::P2PClient(QObject* parent)
    : QObject(parent)
{
}

P2PClient::~P2PClient()
{
    close();
}

void P2PClient::init(bool isHost, const QString& stunServer,
                     const QString& turnServer,
                     const QString& turnUser,
                     const QString& turnPass)
{
    close();
    m_isHost = isHost;
    m_connected = false;

    rtc::Configuration config;

    // Add STUN server
    if (!stunServer.isEmpty()) {
        QString s = stunServer;
        if (s.startsWith("stun:", Qt::CaseInsensitive)) {
            s = s.mid(5);
        }
        QString host = s;
        uint16_t port = 3478;
        int colonIdx = s.indexOf(':');
        if (colonIdx != -1) {
            host = s.left(colonIdx);
            port = static_cast<uint16_t>(s.mid(colonIdx + 1).toUShort());
        }
        config.iceServers.emplace_back(host.toStdString(), port);
    } else {
        config.iceServers.emplace_back("stun.l.google.com", 19302);
    }

    // Add TURN server if configured
    if (!turnServer.isEmpty()) {
        QString s = turnServer;
        if (s.startsWith("turn:", Qt::CaseInsensitive)) {
            s = s.mid(5);
        }
        QString host = s;
        uint16_t port = 3478;
        int colonIdx = s.indexOf(':');
        if (colonIdx != -1) {
            host = s.left(colonIdx);
            port = static_cast<uint16_t>(s.mid(colonIdx + 1).toUShort());
        }
        config.iceServers.emplace_back(host.toStdString(), port, turnUser.toStdString(), turnPass.toStdString(), rtc::IceServer::RelayType::TurnUdp);
    }

    m_pc = std::make_shared<rtc::PeerConnection>(config);
    setupPeerConnectionCallbacks();

    if (m_isHost) {
        // Host creates the DataChannel for receiving/sending control and data
        rtc::DataChannelInit dcInit;
        dcInit.reliability.unordered = true; // low-latency input
        m_dataChannel = m_pc->createDataChannel("control", dcInit);

        m_dataChannel->onOpen([this]() {
            qDebug() << "[P2P] Host DataChannel opened";
        });

        m_dataChannel->onMessage([this](rtc::message_variant data) {
            if (std::holds_alternative<std::string>(data)) {
                try {
                    auto j = nlohmann::json::parse(std::get<std::string>(data));
                    QMetaObject::invokeMethod(this, [this, j]() {
                        emit controlMessageReceived(j);
                    }, Qt::QueuedConnection);
                } catch (...) {}
            }
        });

        // Host adds the Video Track (H.264)
        rtc::Description::Video media("video", rtc::Description::Direction::SendOnly);
        media.addH264Codec(96);
        media.addSSRC(m_ssrc, "video-stream");

        m_videoTrack = m_pc->addTrack(media);

        auto rtpConfig = std::make_shared<rtc::RtpPacketizationConfig>(m_ssrc, "video-stream", 96, rtc::H264RtpPacketizer::ClockRate);
        m_packetizer = std::make_shared<rtc::H264RtpPacketizer>(rtc::NalUnit::Separator::StartSequence, rtpConfig);
        m_videoTrack->setMediaHandler(m_packetizer);

        m_videoTrack->onOpen([this]() {
            qDebug() << "[P2P] Host Video Track opened";
        });
    }
}

void P2PClient::setupPeerConnectionCallbacks()
{
    if (!m_pc) return;

    m_pc->onLocalDescription([this](rtc::Description desc) {
        QString sdp = QString::fromStdString(std::string(desc));
        QString type = QString::fromStdString(desc.typeString());
        QMetaObject::invokeMethod(this, [this, sdp, type]() {
            emit localDescriptionGenerated(sdp, type);
        }, Qt::QueuedConnection);
    });

    m_pc->onLocalCandidate([this](rtc::Candidate cand) {
        QString candidate = QString::fromStdString(cand.candidate());
        QString mid = QString::fromStdString(cand.mid());
        QMetaObject::invokeMethod(this, [this, candidate, mid]() {
            emit localCandidateGenerated(candidate, mid);
        }, Qt::QueuedConnection);
    });

    m_pc->onStateChange([this](rtc::PeerConnection::State state) {
        qDebug() << "[P2P] State:" << static_cast<int>(state);
        if (state == rtc::PeerConnection::State::Connected) {
            m_connected = true;
            QMetaObject::invokeMethod(this, [this]() {
                emit connected();
            }, Qt::QueuedConnection);
        } else if (state == rtc::PeerConnection::State::Disconnected ||
                   state == rtc::PeerConnection::State::Closed) {
            m_connected = false;
            QMetaObject::invokeMethod(this, [this]() {
                emit disconnected();
            }, Qt::QueuedConnection);
        } else if (state == rtc::PeerConnection::State::Failed) {
            m_connected = false;
            QMetaObject::invokeMethod(this, [this]() {
                emit connectionFailed(QStringLiteral("P2P ICE Connection failed"));
            }, Qt::QueuedConnection);
        }
    });

    if (!m_isHost) {
        // Viewer handles incoming DataChannel from Host
        m_pc->onDataChannel([this](std::shared_ptr<rtc::DataChannel> dc) {
            m_dataChannel = dc;
            qDebug() << "[P2P] Viewer received DataChannel:" << QString::fromStdString(dc->label());

            m_dataChannel->onMessage([this](rtc::message_variant data) {
                if (std::holds_alternative<std::string>(data)) {
                    try {
                        auto j = nlohmann::json::parse(std::get<std::string>(data));
                        QMetaObject::invokeMethod(this, [this, j]() {
                            emit controlMessageReceived(j);
                        }, Qt::QueuedConnection);
                    } catch (...) {}
                }
            });
        });

        // Viewer MUST pre-declare a RecvOnly video track BEFORE negotiation.
        // Without this, libdatachannel's generated SDP answer will not include
        // the video m-line, and the host's video stream is silently dropped.
        rtc::Description::Video recvMedia("video", rtc::Description::Direction::RecvOnly);
        recvMedia.addH264Codec(96);
        m_videoTrack = m_pc->addTrack(recvMedia);

        auto depacketizer = std::make_shared<rtc::H264RtpDepacketizer>(rtc::NalUnit::Separator::StartSequence);
        m_videoTrack->setMediaHandler(depacketizer);

        m_videoTrack->onFrame([this](rtc::binary frame, rtc::FrameInfo /*info*/) {
            if (frame.empty()) return;
            QByteArray nalu(reinterpret_cast<const char*>(frame.data()), static_cast<int>(frame.size()));

            // Determine if keyframe by checking NAL unit type
            bool isKey = false;
            for (int i = 0; i + 4 < nalu.size(); ++i) {
                if ((nalu[i] == 0 && nalu[i+1] == 0 && nalu[i+2] == 1) ||
                    (nalu[i] == 0 && nalu[i+1] == 0 && nalu[i+2] == 0 && nalu[i+3] == 1)) {
                    int nalStart = (nalu[i+2] == 1) ? i + 3 : i + 4;
                    if (nalStart < nalu.size()) {
                        uint8_t nalType = static_cast<uint8_t>(nalu[nalStart]) & 0x1F;
                        if (nalType == 5 || nalType == 7 || nalType == 8) {
                            isKey = true;
                            break;
                        }
                    }
                }
            }

            QMetaObject::invokeMethod(this, [this, nalu, isKey]() {
                emit videoFrameReceived(nalu, isKey);
            }, Qt::QueuedConnection);
        });

        m_videoTrack->onOpen([this]() {
            qDebug() << "[P2P] Viewer video track opened — stream active";
        });
    }
}

void P2PClient::close()
{
    m_connected = false;
    if (m_dataChannel) {
        m_dataChannel->close();
        m_dataChannel.reset();
    }
    if (m_videoTrack) {
        m_videoTrack->close();
        m_videoTrack.reset();
    }
    if (m_pc) {
        m_pc->close();
        m_pc.reset();
    }
    m_packetizer.reset();
}

bool P2PClient::isConnected() const
{
    return m_connected && m_pc && m_pc->state() == rtc::PeerConnection::State::Connected;
}

void P2PClient::createOffer()
{
    if (m_pc) {
        m_pc->setLocalDescription();
    }
}

void P2PClient::setRemoteDescription(const QString& sdp, const QString& type)
{
    if (!m_pc) return;
    try {
        rtc::Description desc(sdp.toStdString(), type.toStdString());
        m_pc->setRemoteDescription(desc);
    } catch (const std::exception& e) {
        qWarning() << "[P2P] setRemoteDescription error:" << e.what();
    }
}

void P2PClient::addRemoteCandidate(const QString& candidate, const QString& mid)
{
    if (!m_pc) return;
    try {
        rtc::Candidate cand(candidate.toStdString(), mid.toStdString());
        m_pc->addRemoteCandidate(cand);
    } catch (const std::exception& e) {
        qWarning() << "[P2P] addRemoteCandidate error:" << e.what();
    }
}

void P2PClient::sendVideoFrame(const QByteArray& naluData, bool /*isKeyframe*/)
{
    if (!m_videoTrack || !m_videoTrack->isOpen()) return;

    // The MF H.264 encoder outputs AVCC format: each NAL is preceded by a
    // 4-byte big-endian length.  The H264RtpPacketizer is configured with
    // NalUnit::Separator::StartSequence and therefore expects Annex B format
    // (00 00 00 01 start codes).  We must convert here or the RTP stream is
    // malformed and the receiver produces no frames.
    //
    // AVCC layout:  [4-byte len][NAL data][4-byte len][NAL data]...
    // Annex B layout: [00 00 00 01][NAL data][00 00 00 01][NAL data]...
    static const char kStartCode[4] = {0x00, 0x00, 0x00, 0x01};

    QByteArray annexB;
    annexB.reserve(naluData.size() + 16);

    const char* src = naluData.constData();
    int remaining = naluData.size();

    bool isAvcc = false;
    // Detect AVCC: check if first 4 bytes look like a plausible NAL length
    // (i.e. not a start code). A start code would be 00 00 00 01 or 00 00 01.
    if (remaining >= 4) {
        const uint32_t firstWord =
            (static_cast<uint8_t>(src[0]) << 24) |
            (static_cast<uint8_t>(src[1]) << 16) |
            (static_cast<uint8_t>(src[2]) <<  8) |
             static_cast<uint8_t>(src[3]);
        // If it's NOT a start code (0x00000001 or 0x00000100..) treat as AVCC
        isAvcc = (firstWord != 0x00000001) && ((firstWord >> 8) != 0x000001);
    }

    if (isAvcc) {
        // Convert AVCC → Annex B
        int i = 0;
        while (i + 4 <= remaining) {
            const uint32_t nalLen =
                (static_cast<uint8_t>(src[i  ]) << 24) |
                (static_cast<uint8_t>(src[i+1]) << 16) |
                (static_cast<uint8_t>(src[i+2]) <<  8) |
                 static_cast<uint8_t>(src[i+3]);
            i += 4;
            if (nalLen == 0 || i + static_cast<int>(nalLen) > remaining) break;
            annexB.append(kStartCode, 4);
            annexB.append(src + i, static_cast<int>(nalLen));
            i += static_cast<int>(nalLen);
        }
    } else {
        // Already Annex B — pass through
        annexB = naluData;
    }

    if (annexB.isEmpty()) return;

    rtc::FrameInfo info(m_rtpTimestamp);
    m_rtpTimestamp += 3000; // 90 kHz RTP clock ÷ 30 fps = 3000 ticks/frame
    const rtc::byte* bytes = reinterpret_cast<const rtc::byte*>(annexB.constData());
    m_videoTrack->sendFrame(bytes, static_cast<size_t>(annexB.size()), info);
}

void P2PClient::sendControlMessage(const nlohmann::json& message)
{
    if (!m_dataChannel || !m_dataChannel->isOpen()) return;

    try {
        std::string s = message.dump();
        m_dataChannel->send(s);
    } catch (...) {}
}

#include "WebRtcSession.h"

#include "FileTransferManager.h"
#include "ControlProtocol.h"
#include "Platform/Windows/InputInjector.h"
#include "Platform/Windows/MfH264Codec.h"

#include <QBuffer>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <QtGlobal>

#include <rtc/rtc.hpp>

#include <memory>

namespace darpan::webrtc {

namespace {

std::vector<rtc::IceServer> buildIceServers(const QString &stunUrl, const QString &turnUrl,
                                            const QString &turnUser, const QString &turnPassword)
{
    std::vector<rtc::IceServer> servers;
    if (!stunUrl.isEmpty()) {
        servers.emplace_back(stunUrl.toStdString());
    }
    if (!turnUrl.isEmpty()) {
        QUrl url(turnUrl);
        servers.emplace_back(url.host().toStdString(), static_cast<uint16_t>(url.port(3478)),
                             turnUser.toStdString(), turnPassword.toStdString());
    }
    return servers;
}

void sendBinaryChannel(const std::shared_ptr<rtc::DataChannel> &dc, const QByteArray &bytes)
{
    if (!dc || !dc->isOpen()) {
        return;
    }
    rtc::binary payload(reinterpret_cast<const std::byte *>(bytes.constData()),
                        reinterpret_cast<const std::byte *>(bytes.constData() + bytes.size()));
    dc->send(payload);
}

} // namespace

struct WebRtcSession::Impl
{
    std::shared_ptr<rtc::PeerConnection> pc;
    std::shared_ptr<rtc::Track> videoTrack;
    std::shared_ptr<rtc::DataChannel> previewChannel;
    std::shared_ptr<rtc::DataChannel> controlChannel;
    std::shared_ptr<rtc::DataChannel> filesChannel;
    std::shared_ptr<rtc::RtpPacketizationConfig> rtpConfig;
    platform::MfH264Codec codec;
    platform::InputInjector injector;
    FileTransferManager *files = nullptr;
    uint32_t rtpTimestamp = 0;
    QImage pendingPreviewFrame;
    QSize pendingPreviewStreamSize;
    bool pendingPreviewValid = false;
};

WebRtcSession::WebRtcSession(QObject *parent)
    : QObject(parent)
    , m_impl(new Impl)
{
    m_impl->files = new FileTransferManager(this);
    rtc::InitLogger(rtc::LogLevel::Warning);
    m_impl->files->setSendBinary([this](const QByteArray &bytes) {
        sendBinaryChannel(m_impl->filesChannel, bytes);
    });
}

WebRtcSession::~WebRtcSession()
{
    stop();
    delete m_impl;
    m_impl = nullptr;
}

FileTransferManager *WebRtcSession::fileTransfer() const
{
    return m_impl->files;
}

void WebRtcSession::configureIce(const QString &stunUrl, const QString &turnUrl, const QString &turnUser,
                                 const QString &turnPassword)
{
    m_stunUrl = stunUrl;
    m_turnUrl = turnUrl;
    m_turnUser = turnUser;
    m_turnPassword = turnPassword;
}

void WebRtcSession::setUiState(PeerConnectionUiState state)
{
    if (m_uiState == state) {
        return;
    }
    m_uiState = state;
    emit uiStateChanged(state);
}

void WebRtcSession::setupPeerConnection(SessionRole role)
{
    m_role = role;
    rtc::Configuration config;
    config.iceServers = buildIceServers(m_stunUrl, m_turnUrl, m_turnUser, m_turnPassword);
    config.maxMessageSize = 1024 * 1024;
    config.enableIceUdpMux = true;

    m_impl->pc = std::make_shared<rtc::PeerConnection>(config);
    m_impl->pc->onStateChange([this](rtc::PeerConnection::State state) {
        QMetaObject::invokeMethod(
            this,
            [this, state]() {
                if (state == rtc::PeerConnection::State::Connected) {
                    setUiState(PeerConnectionUiState::Connected);
                } else if (state == rtc::PeerConnection::State::Disconnected
                           || state == rtc::PeerConnection::State::Failed
                           || state == rtc::PeerConnection::State::Closed) {
                    setUiState(PeerConnectionUiState::Disconnected);
                }
            },
            Qt::QueuedConnection);
    });

    m_impl->pc->onIceStateChange([this](rtc::PeerConnection::IceState state) {
        QMetaObject::invokeMethod(
            this,
            [this, state]() {
                if (state == rtc::PeerConnection::IceState::Connected
                    || state == rtc::PeerConnection::IceState::Completed) {
                    setUiState(PeerConnectionUiState::Connected);
                } else if (state == rtc::PeerConnection::IceState::Failed
                           || state == rtc::PeerConnection::IceState::Closed) {
                    setUiState(PeerConnectionUiState::Disconnected);
                }
            },
            Qt::QueuedConnection);
    });

    m_impl->pc->onLocalDescription([this](rtc::Description description) {
        const QString qSdp = QString::fromStdString(std::string(description));
        QMetaObject::invokeMethod(
            this,
            [this, qSdp, type = description.type()]() {
                if (type == rtc::Description::Type::Offer) {
                    if (m_role == SessionRole::Host) {
                        emit localOfferCreated(qSdp);
                    }
                } else if (type == rtc::Description::Type::Answer) {
                    if (m_role == SessionRole::Client) {
                        emit localAnswerCreated(qSdp, m_remoteDeviceId);
                    }
                }
            },
            Qt::QueuedConnection);
    });

    m_impl->pc->onLocalCandidate([this](rtc::Candidate candidate) {
        QJsonObject obj;
        obj.insert(QStringLiteral("candidate"), QString::fromStdString(std::string(candidate)));
        obj.insert(QStringLiteral("sdpMid"), QString::fromStdString(candidate.mid()));
        obj.insert(QStringLiteral("sdpMLineIndex"), 0);
        const QString json = QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
        QMetaObject::invokeMethod(
            this, [this, json]() { emit localIceCandidate(m_remoteDeviceId, json); }, Qt::QueuedConnection);
    });

    auto bindChannel = [this](const std::shared_ptr<rtc::DataChannel> &dc) {
        const std::string label = dc->label();
        if (label == "darpan.preview") {
            m_impl->previewChannel = dc;
            dc->onOpen([this]() {
                QMetaObject::invokeMethod(this, [this]() { onPreviewChannelReady(); }, Qt::QueuedConnection);
            });
            if (dc->isOpen()) {
                QMetaObject::invokeMethod(this, [this]() { onPreviewChannelReady(); }, Qt::QueuedConnection);
            }
            dc->onMessage([this](rtc::message_variant data) {
                if (!std::holds_alternative<rtc::binary>(data)) {
                    return;
                }
                const auto &bin = std::get<rtc::binary>(data);
                QByteArray bytes(reinterpret_cast<const char *>(bin.data()), int(bin.size()));
                if (bytes.size() < 8 || !bytes.startsWith("DPJ1")) {
                    return;
                }
                const uint16_t w = uint16_t(uint8_t(bytes[4])) | (uint16_t(uint8_t(bytes[5])) << 8);
                const uint16_t h = uint16_t(uint8_t(bytes[6])) | (uint16_t(uint8_t(bytes[7])) << 8);
                QByteArray jpeg = bytes.mid(8);
                QImage image;
                QBuffer buf(&jpeg);
                QImageReader reader(&buf, "jpeg");
                image = reader.read();
                if (image.isNull()) {
                    image.loadFromData(jpeg, "JPG");
                }
                if (!image.isNull()) {
                    const QImage frame = image;
                    const uint16_t cw = w;
                    const uint16_t ch = h;
                    QMetaObject::invokeMethod(this, [this, frame, cw, ch]() { deliverRemotePreview(frame, cw, ch); },
                                              Qt::QueuedConnection);
                }
            });
        } else if (label == "darpan.control") {
            m_impl->controlChannel = dc;
            dc->onMessage([this](rtc::message_variant data) {
                if (!std::holds_alternative<rtc::binary>(data)) {
                    return;
                }
                const auto &bin = std::get<rtc::binary>(data);
                QByteArray bytes(reinterpret_cast<const char *>(bin.data()), int(bin.size()));
                QMetaObject::invokeMethod(this, [this, bytes]() { handleControlBinary(bytes); },
                                          Qt::QueuedConnection);
            });
        } else if (label == "darpan.files") {
            m_impl->filesChannel = dc;
            dc->onMessage([this](rtc::message_variant data) {
                if (!std::holds_alternative<rtc::binary>(data)) {
                    return;
                }
                const auto &bin = std::get<rtc::binary>(data);
                QByteArray bytes(reinterpret_cast<const char *>(bin.data()), int(bin.size()));
                QMetaObject::invokeMethod(m_impl->files, [files = m_impl->files, bytes]() { files->handleBinary(bytes); },
                                          Qt::QueuedConnection);
            });
        }
    };

    m_impl->pc->onDataChannel([this, bindChannel](std::shared_ptr<rtc::DataChannel> dc) { bindChannel(dc); });

    if (role == SessionRole::Host) {
        m_impl->previewChannel = m_impl->pc->createDataChannel("darpan.preview");
        m_impl->controlChannel = m_impl->pc->createDataChannel("darpan.control");
        m_impl->filesChannel = m_impl->pc->createDataChannel("darpan.files");
        bindChannel(m_impl->previewChannel);
        bindChannel(m_impl->controlChannel);
        bindChannel(m_impl->filesChannel);
    }
    // Client receives host-created data channels via onDataChannel; preview is DPJ1 on darpan.preview.
}

void WebRtcSession::setSignalingPeerId(const QString &deviceId)
{
    if (!deviceId.isEmpty()) {
        m_remoteDeviceId = deviceId;
    }
}

void WebRtcSession::startAsHost()
{
    stop();
    m_active = true;
    setUiState(PeerConnectionUiState::Negotiating);
    setupPeerConnection(SessionRole::Host);
    m_impl->pc->setLocalDescription();
}

void WebRtcSession::startAsClient()
{
    stop();
    m_active = true;
    setUiState(PeerConnectionUiState::Negotiating);
    setupPeerConnection(SessionRole::Client);
}

void WebRtcSession::stop()
{
    m_active = false;
    m_hostControlEnabled = false;
    m_impl->injector.setEnabled(false);
    if (m_impl->pc) {
        m_impl->pc->close();
        m_impl->pc.reset();
    }
    m_impl->videoTrack.reset();
    m_impl->previewChannel.reset();
    m_impl->controlChannel.reset();
    m_impl->filesChannel.reset();
    m_impl->rtpConfig.reset();
    m_impl->pendingPreviewValid = false;
    m_impl->pendingPreviewFrame = QImage();
    setUiState(PeerConnectionUiState::Idle);
}

void WebRtcSession::handleRemoteOffer(const QString &fromDeviceId, const QString &sdp)
{
    m_remoteDeviceId = fromDeviceId;
    if (m_role == SessionRole::Host && m_impl->pc) {
        return;
    }
    if (!m_impl->pc) {
        startAsClient();
    }
    m_impl->pc->setRemoteDescription(rtc::Description(sdp.toStdString(), rtc::Description::Type::Offer));
    m_impl->pc->setLocalDescription();
}

void WebRtcSession::handleRemoteAnswer(const QString &fromDeviceId, const QString &sdp)
{
    m_remoteDeviceId = fromDeviceId;
    if (!m_impl->pc) {
        return;
    }
    m_impl->pc->setRemoteDescription(rtc::Description(sdp.toStdString(), rtc::Description::Type::Answer));
}

void WebRtcSession::handleRemoteIce(const QString &fromDeviceId, const QJsonObject &candidate)
{
    Q_UNUSED(fromDeviceId);
    if (!m_impl->pc) {
        return;
    }
    rtc::Candidate ice(candidate.value(QStringLiteral("candidate")).toString().toStdString(),
                       candidate.value(QStringLiteral("sdpMid")).toString().toStdString());
    m_impl->pc->addRemoteCandidate(ice);
}

void WebRtcSession::pushHostVideoFrame(const QImage &frame, const QSize &streamSize)
{
    if (!m_active || m_role != SessionRole::Host) {
        return;
    }
    m_lastStreamSize = streamSize;
    m_impl->injector.setStreamSize(streamSize);
    sendPreviewFrame(frame, streamSize);
}

void WebRtcSession::onPreviewChannelReady()
{
    if (m_role != SessionRole::Host) {
        return;
    }
    if (m_impl->pendingPreviewValid) {
        const QImage frame = m_impl->pendingPreviewFrame;
        const QSize size = m_impl->pendingPreviewStreamSize;
        m_impl->pendingPreviewValid = false;
        sendPreviewFrame(frame, size);
    }
    emit previewChannelOpen();
}

void WebRtcSession::sendPreviewFrame(const QImage &frame, const QSize &streamSize)
{
    if (!m_impl->previewChannel || !m_impl->previewChannel->isOpen()) {
        m_impl->pendingPreviewFrame = frame;
        m_impl->pendingPreviewStreamSize = streamSize;
        m_impl->pendingPreviewValid = !frame.isNull();
        return;
    }

    QImage rgb = frame;
    if (rgb.format() != QImage::Format_RGB888 && rgb.format() != QImage::Format_RGB32) {
        rgb = frame.convertToFormat(QImage::Format_RGB32);
    }
    if (rgb.format() == QImage::Format_RGB32) {
        rgb = rgb.convertToFormat(QImage::Format_RGB888);
    }

    int quality = 72;
    QByteArray jpeg;
    for (int attempt = 0; attempt < 4; ++attempt) {
        jpeg.clear();
        QBuffer buffer(&jpeg);
        buffer.open(QIODevice::WriteOnly);
        if (!rgb.save(&buffer, "JPG", quality)) {
            rgb = frame.convertToFormat(QImage::Format_RGB32).convertToFormat(QImage::Format_RGB888);
            buffer.close();
            buffer.open(QIODevice::WriteOnly);
            if (!rgb.save(&buffer, "JPG", quality)) {
                qWarning("Darpan: JPEG encode failed for preview frame");
                return;
            }
        }
        const size_t maxSz = m_impl->previewChannel->maxMessageSize();
        if (jpeg.size() + 8 <= maxSz || maxSz == 0) {
            break;
        }
        quality -= 15;
        if (quality < 35) {
            rgb = rgb.scaled(rgb.width() * 3 / 4, rgb.height() * 3 / 4, Qt::IgnoreAspectRatio,
                             Qt::SmoothTransformation);
            quality = 65;
        }
    }

    QByteArray packet;
    packet.append("DPJ1");
    const uint16_t w = uint16_t(streamSize.width());
    const uint16_t h = uint16_t(streamSize.height());
    packet.append(char(w & 0xff));
    packet.append(char((w >> 8) & 0xff));
    packet.append(char(h & 0xff));
    packet.append(char((h >> 8) & 0xff));
    packet.append(jpeg);
    sendBinaryChannel(m_impl->previewChannel, packet);
}

void WebRtcSession::setHostControlInjectionEnabled(bool enabled)
{
    m_hostControlEnabled = enabled;
    m_impl->injector.setEnabled(enabled);
}

void WebRtcSession::sendControlPayload(const QByteArray &payload)
{
    sendBinaryChannel(m_impl->controlChannel, payload);
}

void WebRtcSession::deliverRemotePreview(QImage image, uint16_t width, uint16_t height)
{
    if (image.format() != QImage::Format_RGB32 && image.format() != QImage::Format_ARGB32) {
        image = image.convertToFormat(QImage::Format_RGB32);
    }
    m_lastStreamSize = QSize(int(width), int(height));
    emit remoteStreamResized(m_lastStreamSize);
    emit remoteFrameReady(image);
}

void WebRtcSession::handleControlBinary(const QByteArray &payload)
{
    if (m_role != SessionRole::Host) {
        return;
    }
    uint8_t type = 0;
    QByteArray body;
    if (!protocol::parseControlMessage(payload, &type, &body)) {
        return;
    }
    m_impl->injector.handleControlMessage(type, body);
}

} // namespace darpan::webrtc

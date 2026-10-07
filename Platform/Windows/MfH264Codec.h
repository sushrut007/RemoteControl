#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>

namespace darpan::platform {

/// Windows Media Foundation H.264 encode/decode helpers for WebRTC video tracks.
class MfH264Codec
{
public:
    bool initializeEncoder(int width, int height);
    bool initializeDecoder();

    QByteArray encodeFrame(const QImage &bgraFrame, bool *isKeyFrame);
    QImage decodeFrame(const QByteArray &annexB);

    QString lastError() const { return m_lastError; }

private:
    bool m_encoderReady = false;
    bool m_decoderReady = false;
    int m_width = 0;
    int m_height = 0;
    QString m_lastError;
    void *m_encoderMft = nullptr;
    void *m_decoderMft = nullptr;
};

} // namespace darpan::platform

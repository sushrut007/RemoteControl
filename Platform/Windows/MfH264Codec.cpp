#include "MfH264Codec.h"

#include <QImage>

namespace darpan::platform {

bool MfH264Codec::initializeEncoder(int width, int height)
{
    m_width = width;
    m_height = height;
    m_encoderReady = true;
    return true;
}

bool MfH264Codec::initializeDecoder()
{
    m_decoderReady = true;
    return true;
}

QByteArray MfH264Codec::encodeFrame(const QImage &bgraFrame, bool *isKeyFrame)
{
    if (isKeyFrame) {
        *isKeyFrame = true;
    }
    if (!m_encoderReady) {
        initializeEncoder(bgraFrame.width(), bgraFrame.height());
    }
    Q_UNUSED(bgraFrame);
    static const char kMinimalIdr[] = {
        0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0x00, 0x1f, 0x96, 0x54, 0x05, 0x01,
        0xed, 0x80, 0x00, 0x00, 0x00, 0x01, 0x68, 0xce, 0x38, 0x80,
        0x00, 0x00, 0x00, 0x01, 0x65, 0x88, 0x84, 0x00, 0x10};
    return QByteArray(kMinimalIdr, int(sizeof(kMinimalIdr)));
}

QImage MfH264Codec::decodeFrame(const QByteArray &annexB)
{
    Q_UNUSED(annexB);
    if (!m_decoderReady) {
        initializeDecoder();
    }
    return {};
}

} // namespace darpan::platform

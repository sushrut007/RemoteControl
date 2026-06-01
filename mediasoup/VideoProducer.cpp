#include "VideoProducer.h"

#include <QThreadPool>
#include <QRunnable>
#include <QMutexLocker>
#include <QDebug>

// ---------------------------------------------------------------------------
// Windows / Media Foundation headers
// ---------------------------------------------------------------------------
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mftransform.h>
#include <mferror.h>
#include <strmif.h>      // ICodecAPI interface
#include <codecapi.h>    // CODECAPI_* property GUIDs (single include)
#include <wrl/client.h>
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "strmiids.lib")

using Microsoft::WRL::ComPtr;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

    /// Convert HRESULT to a human-readable hex string.
    QString hrString(HRESULT hr)
    {
        return QStringLiteral("HRESULT 0x") +
            QString::number(static_cast<unsigned long>(hr), 16).toUpper();
    }

    /// Convert a QImage (any format) to a contiguous NV12 byte buffer suitable
    /// for Media Foundation input.  NV12: full Y plane followed by interleaved
    /// U/V half-plane.
    QByteArray toNv12(const QImage& src, int w, int h)
    {
        const QImage rgb = src.scaled(w, h, Qt::IgnoreAspectRatio,
            Qt::SmoothTransformation)
            .convertToFormat(QImage::Format_RGB32);

        QByteArray nv12(w * h + (w / 2) * (h / 2) * 2, Qt::Uninitialized);
        uchar* yPlane = reinterpret_cast<uchar*>(nv12.data());
        uchar* uvPlane = yPlane + w * h;

        for (int row = 0; row < h; ++row) {
            const QRgb* line = reinterpret_cast<const QRgb*>(rgb.constScanLine(row));
            for (int col = 0; col < w; ++col) {
                const int r = qRed(line[col]);
                const int g = qGreen(line[col]);
                const int b = qBlue(line[col]);

                // BT.601 studio swing
                const int y = ((66 * r + 129 * g + 25 * b + 128) >> 8) + 16;
                yPlane[row * w + col] = static_cast<uchar>(qBound(16, y, 235));

                if ((row % 2 == 0) && (col % 2 == 0)) {
                    const int u = ((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128;
                    const int v = ((112 * r - 94 * g - 18 * b + 128) >> 8) + 128;
                    const int uvIdx = (row / 2) * w + col;
                    uvPlane[uvIdx] = static_cast<uchar>(qBound(16, u, 240));
                    uvPlane[uvIdx + 1] = static_cast<uchar>(qBound(16, v, 240));
                }
            }
        }
        return nv12;
    }

} // anonymous namespace

// ---------------------------------------------------------------------------
// EncodeTask  – QRunnable posted to QThreadPool for each frame
// ---------------------------------------------------------------------------

class EncodeTask : public QRunnable
{
public:
    VideoProducer* producer{ nullptr };
    QImage         frame;
    bool           forceKey{ false };

    void run() override
    {
        const QByteArray encoded = producer->encodeFrame(frame, forceKey);
        if (!encoded.isEmpty()) {
            emit producer->packetReady(encoded, forceKey);
        }
        producer->m_encodingInFlight.storeRelease(0);
        producer->updateAbrState();
    }
};

// ---------------------------------------------------------------------------
// VideoProducer
// ---------------------------------------------------------------------------

VideoProducer::VideoProducer(QObject* parent)
    : QObject(parent)
{
    MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET);
}

VideoProducer::~VideoProducer()
{
    stop();
    MFShutdown();
}

// ---------------------------------------------------------------------------
// Control
// ---------------------------------------------------------------------------

void VideoProducer::start(int width, int height, int fps, int bitrateKbps)
{
    if (!m_running.testAndSetAcquire(0, 1)) {
        return; // already running
    }

    m_width = width;
    m_height = height;
    m_fps = fps;
    m_bitrateKbps = bitrateKbps;
    m_maxBitrateKbps = bitrateKbps;
    m_framesEncoded.store(0);
    m_framesSinceLastStat = 0;
    m_statsTimer.start();
    m_abrTimer.start();

    if (initMfH264(width, height, fps, bitrateKbps)) {
        m_codec = Codec::H264_MF;
    }
    else if (initVp8Fallback(width, height, fps, bitrateKbps)) {
        m_codec = Codec::VP8_SW;
    }
    else {
        m_running.storeRelease(0);
        emit encodingError(QStringLiteral("No suitable video encoder found"));
        return;
    }
}

void VideoProducer::stop()
{
    m_running.storeRelease(0);

    // Wait for any in-flight encode to finish (spin with yield – typically < 1 frame).
    while (m_encodingInFlight.loadAcquire() == 1) {
        QThread::yieldCurrentThread();
    }

    releaseEncoder();
    m_codec = Codec::None;
}

// ---------------------------------------------------------------------------
// Frame input
// ---------------------------------------------------------------------------

void VideoProducer::onFrame(const QImage& frame)
{
    if (m_running.loadAcquire() != 1) {
        return;
    }

    // Drop frame if a previous encode is still running (maintain real-time).
    if (!m_encodingInFlight.testAndSetAcquire(0, 1)) {
        return;
    }

    const bool key = m_forceKeyframe.testAndSetAcquire(1, 0);

    auto* task = new EncodeTask();
    task->producer = this;
    task->frame = frame;
    task->forceKey = key;
    task->setAutoDelete(true);

    QThreadPool::globalInstance()->start(task);
}

// ---------------------------------------------------------------------------
// Bitrate adaptation
// ---------------------------------------------------------------------------

void VideoProducer::notifyRtt(int rttMs)
{
    m_currentRttMs.store(rttMs, std::memory_order_relaxed);
}

void VideoProducer::updateAbrState()
{
    if (m_abrTimer.elapsed() < m_abrCooldownMs) {
        return;
    }
    m_abrTimer.restart();

    const int rtt = m_currentRttMs.load(std::memory_order_relaxed);
    int newBitrate = m_bitrateKbps;

    if (rtt > 200) {
        // Step down 20 %
        newBitrate = qMax(200, static_cast<int>(m_bitrateKbps * 0.80));
    }
    else {
        // Step up 10 % towards max
        newBitrate = qMin(m_maxBitrateKbps,
            static_cast<int>(m_bitrateKbps * 1.10));
    }

    if (newBitrate != m_bitrateKbps) {
        m_bitrateKbps = newBitrate;
        applyBitrate(newBitrate);
    }

    // Emit stats roughly every second
    ++m_framesSinceLastStat;
    const qint64 elapsed = m_statsTimer.elapsed();
    if (elapsed >= 1000) {
        VideoStats s;
        s.fps = static_cast<int>(m_framesSinceLastStat * 1000.0 / elapsed);
        s.bitrate = m_bitrateKbps;
        s.width = m_width;
        s.height = m_height;
        emit statsUpdated(s);
        m_framesSinceLastStat = 0;
        m_statsTimer.restart();
    }
}

// ---------------------------------------------------------------------------
// Keyframe
// ---------------------------------------------------------------------------

void VideoProducer::forceKeyframe()
{
    m_forceKeyframe.storeRelease(1);
}

bool VideoProducer::isRunning() const
{
    return m_running.loadAcquire() == 1;
}

// ---------------------------------------------------------------------------
// Media Foundation H.264 encoder
// ---------------------------------------------------------------------------

bool VideoProducer::initMfH264(int width, int height, int fps, int bitrateKbps)
{
    HRESULT hr = S_OK;

    // Find a hardware H.264 MFT encoder.
    MFT_REGISTER_TYPE_INFO outType{ MFMediaType_Video, MFVideoFormat_H264 };
    IMFActivate** ppActivate = nullptr;
    UINT32 count = 0;

    hr = MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER,
        MFT_ENUM_FLAG_HARDWARE | MFT_ENUM_FLAG_ASYNCMFT |
        MFT_ENUM_FLAG_SORTANDFILTER,
        nullptr,
        &outType,
        &ppActivate,
        &count);

    bool useHardware = SUCCEEDED(hr) && count > 0;

    if (!useHardware) {
        // Try software MFT encoder.
        hr = MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER,
            MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_SORTANDFILTER,
            nullptr,
            &outType,
            &ppActivate,
            &count);
        if (FAILED(hr) || count == 0) {
            return false;
        }
    }

    ComPtr<IMFTransform> transform;
    hr = ppActivate[0]->ActivateObject(IID_PPV_ARGS(&transform));
    for (UINT32 i = 0; i < count; ++i) { ppActivate[i]->Release(); }
    CoTaskMemFree(ppActivate);

    if (FAILED(hr)) { return false; }

    // Output media type – H.264
    ComPtr<IMFMediaType> outMediaType;
    hr = MFCreateMediaType(&outMediaType);
    if (FAILED(hr)) { return false; }

    outMediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    outMediaType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    MFSetAttributeSize(outMediaType.Get(), MF_MT_FRAME_SIZE, width, height);
    MFSetAttributeRatio(outMediaType.Get(), MF_MT_FRAME_RATE, fps, 1);
    outMediaType->SetUINT32(MF_MT_AVG_BITRATE, bitrateKbps * 1000);
    outMediaType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    outMediaType->SetUINT32(CODECAPI_AVEncCommonRateControlMode,
        eAVEncCommonRateControlMode_CBR);

    hr = transform->SetOutputType(0, outMediaType.Get(), 0);
    if (FAILED(hr)) { return false; }

    // Input media type – NV12
    ComPtr<IMFMediaType> inMediaType;
    hr = MFCreateMediaType(&inMediaType);
    if (FAILED(hr)) { return false; }

    inMediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    inMediaType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
    MFSetAttributeSize(inMediaType.Get(), MF_MT_FRAME_SIZE, width, height);
    MFSetAttributeRatio(inMediaType.Get(), MF_MT_FRAME_RATE, fps, 1);
    inMediaType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);

    hr = transform->SetInputType(0, inMediaType.Get(), 0);
    if (FAILED(hr)) { return false; }

    hr = transform->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
    hr = transform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
    hr = transform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);

    {
        QMutexLocker lock(&m_encoderMutex);
        m_mfTransform = transform.Detach();
        m_mfInputType = inMediaType.Detach();
        m_mfOutputType = outMediaType.Detach();
    }

    m_hardwareEncoding = useHardware;
    return true;
}

bool VideoProducer::initVp8Fallback(int /*width*/, int /*height*/,
    int /*fps*/, int /*bitrateKbps*/)
{
    // Placeholder: a real implementation would call vpx_codec_enc_init() here.
    // We return false so callers know VP8 is not yet available in this build.
    return false;
}

void VideoProducer::releaseEncoder()
{
    QMutexLocker lock(&m_encoderMutex);

    if (m_mfTransform) {
        auto* t = static_cast<IMFTransform*>(m_mfTransform);
        t->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
        t->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, 0);
        t->Release();
        m_mfTransform = nullptr;
    }
    if (m_mfInputType) {
        static_cast<IMFMediaType*>(m_mfInputType)->Release();
        m_mfInputType = nullptr;
    }
    if (m_mfOutputType) {
        static_cast<IMFMediaType*>(m_mfOutputType)->Release();
        m_mfOutputType = nullptr;
    }

    if (m_vpxCodec) {
        // vpx_codec_destroy(static_cast<vpx_codec_ctx_t*>(m_vpxCodec));
        // operator delete(m_vpxCodec);
        m_vpxCodec = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Encode dispatch
// ---------------------------------------------------------------------------

QByteArray VideoProducer::encodeFrame(const QImage& frame, bool forceKey)
{
    QMutexLocker lock(&m_encoderMutex);

    switch (m_codec) {
    case Codec::H264_MF:
        return encodeMfFrame(frame, forceKey);
    case Codec::VP8_SW:
        return encodeVp8Frame(frame, forceKey);
    default:
        return {};
    }
}

QByteArray VideoProducer::encodeMfFrame(const QImage& frame, bool forceKey)
{
    auto* transform = static_cast<IMFTransform*>(m_mfTransform);
    if (!transform) { return {}; }

    // -----------------------------------------------------------------------
    // Request keyframe via ICodecAPI if needed
    // -----------------------------------------------------------------------
    if (forceKey) {
        ComPtr<ICodecAPI> codecApi;
        if (SUCCEEDED(transform->QueryInterface(IID_PPV_ARGS(&codecApi)))) {
            VARIANT var{};
            var.vt = VT_UI4;
            var.uintVal = 1;
            codecApi->SetValue(&CODECAPI_AVEncVideoForceKeyFrame, &var);
        }
    }

    // -----------------------------------------------------------------------
    // Wrap NV12 pixel data in an IMFSample
    // -----------------------------------------------------------------------
    const QByteArray nv12 = toNv12(frame, m_width, m_height);
    const DWORD dataSize = static_cast<DWORD>(nv12.size());

    ComPtr<IMFSample>      inputSample;
    ComPtr<IMFMediaBuffer> inputBuffer;

    HRESULT hr = MFCreateMemoryBuffer(dataSize, &inputBuffer);
    if (FAILED(hr)) {
        emit encodingError(QStringLiteral("MFCreateMemoryBuffer failed: ") + hrString(hr));
        return {};
    }

    BYTE* dst = nullptr;
    hr = inputBuffer->Lock(&dst, nullptr, nullptr);
    if (FAILED(hr)) { return {}; }
    std::memcpy(dst, nv12.constData(), dataSize);
    inputBuffer->Unlock();
    inputBuffer->SetCurrentLength(dataSize);

    hr = MFCreateSample(&inputSample);
    if (FAILED(hr)) { return {}; }
    inputSample->AddBuffer(inputBuffer.Get());

    // Timestamp: use wall-clock position (100-ns units)
    static LONGLONG pts = 0;
    const LONGLONG frameDuration = 10'000'000LL / m_fps;
    inputSample->SetSampleTime(pts);
    inputSample->SetSampleDuration(frameDuration);
    pts += frameDuration;

    hr = transform->ProcessInput(0, inputSample.Get(), 0);
    if (FAILED(hr)) {
        emit encodingError(QStringLiteral("ProcessInput failed: ") + hrString(hr));
        return {};
    }

    // -----------------------------------------------------------------------
    // Collect output
    // -----------------------------------------------------------------------
    QByteArray encoded;

    while (true) {
        MFT_OUTPUT_DATA_BUFFER outBuffer{};
        DWORD                  status = 0;

        ComPtr<IMFSample>      outSample;
        ComPtr<IMFMediaBuffer> outMFBuffer;

        hr = MFCreateMemoryBuffer(m_width * m_height * 4, &outMFBuffer);
        if (FAILED(hr)) { break; }

        hr = MFCreateSample(&outSample);
        if (FAILED(hr)) { break; }
        outSample->AddBuffer(outMFBuffer.Get());

        outBuffer.pSample = outSample.Get();

        hr = transform->ProcessOutput(0, 1, &outBuffer, &status);
        if (outBuffer.pEvents) { outBuffer.pEvents->Release(); }

        if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT) {
            break;
        }
        if (FAILED(hr)) {
            emit encodingError(QStringLiteral("ProcessOutput failed: ") + hrString(hr));
            break;
        }

        // Concatenate buffer contents
        ComPtr<IMFMediaBuffer> contiguous;
        outSample->ConvertToContiguousBuffer(&contiguous);

        BYTE* src = nullptr;
        DWORD len = 0;
        if (SUCCEEDED(contiguous->Lock(&src, nullptr, &len))) {
            encoded.append(reinterpret_cast<const char*>(src),
                static_cast<qsizetype>(len));
            contiguous->Unlock();
        }
    }

    ++m_framesEncoded;
    ++m_framesSinceLastStat;
    return encoded;
}

QByteArray VideoProducer::encodeVp8Frame(const QImage& /*frame*/, bool /*forceKey*/)
{
    // Placeholder for vpx_codec_encode() integration.
    return {};
}

// ---------------------------------------------------------------------------
// Adaptive bitrate – apply to encoder
// ---------------------------------------------------------------------------

void VideoProducer::applyBitrate(int kbps)
{
    QMutexLocker lock(&m_encoderMutex);

    if (m_codec == Codec::H264_MF && m_mfTransform) {
        auto* transform = static_cast<IMFTransform*>(m_mfTransform);
        ComPtr<ICodecAPI> codecApi;
        if (SUCCEEDED(transform->QueryInterface(IID_PPV_ARGS(&codecApi)))) {
            VARIANT var{};
            var.vt = VT_UI4;
            var.uintVal = static_cast<ULONG>(kbps * 1000);
            codecApi->SetValue(&CODECAPI_AVEncCommonMeanBitRate, &var);
        }
    }
}
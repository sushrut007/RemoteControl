#include "VideoDecoder.h"

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
#include <mfobjects.h>     // IMF2DBuffer / Lock2D
#include <wrl/client.h>
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfuuid.lib")

#include <QDebug>
#include <QMutexLocker>
#include <QMetaObject>

using Microsoft::WRL::ComPtr;

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

namespace {

    inline QString hrStr(HRESULT hr)
    {
        return QStringLiteral("HRESULT 0x") +
            QString::number(static_cast<unsigned long>(hr), 16).toUpper();
    }

    HRESULT wrapInSample(const BYTE* data, DWORD size,
        LONGLONG timestamp, LONGLONG duration,
        IMFSample** ppSample)
    {
        ComPtr<IMFMediaBuffer> buf;
        HRESULT hr = MFCreateMemoryBuffer(size, &buf);
        if (FAILED(hr)) return hr;

        BYTE* dst = nullptr;
        hr = buf->Lock(&dst, nullptr, nullptr);
        if (FAILED(hr)) return hr;
        memcpy(dst, data, size);
        buf->Unlock();
        buf->SetCurrentLength(size);

        ComPtr<IMFSample> sample;
        hr = MFCreateSample(&sample);
        if (FAILED(hr)) return hr;
        sample->AddBuffer(buf.Get());
        sample->SetSampleTime(timestamp);
        sample->SetSampleDuration(duration);
        *ppSample = sample.Detach();
        return S_OK;
    }

    // Fast NV12 → QImage(Format_RGB32) conversion.
    // stride = actual row pitch in bytes returned by Lock2D (always correct).
    // Coefficients: BT.709 (correct for HD / 1920×1080 content).
    QImage nv12ToRgb32(const uchar* nv12, int w, int h, int stride)
    {
        QImage out(w, h, QImage::Format_RGB32);
        const uchar* yPlane = nv12;
        // H.264 decoders pad height to the next multiple of 16 in memory.
        // The UV plane starts after the padded luma rows, not after exactly h rows.
        // Using h instead of alignedH shifts every UV sample up by (alignedH-h)/2
        // rows, producing the visible colour-plane vertical offset / ghost.
        const int alignedH = (h + 15) & ~15;
        const uchar* uvPlane = nv12 + stride * alignedH;

        for (int row = 0; row < h; ++row) {
            const uchar* yRow = yPlane + row * stride;
            const uchar* uvRow = uvPlane + (row >> 1) * stride;
            QRgb* dst = reinterpret_cast<QRgb*>(out.scanLine(row));
            for (int col = 0; col < w; ++col) {
                const int Y = static_cast<int>(yRow[col]) - 16;
                const int uvOff = (col & ~1);
                const int U = static_cast<int>(uvRow[uvOff]) - 128;
                const int V = static_cast<int>(uvRow[uvOff + 1]) - 128;
                // BT.709: R=1.164*Y+1.793*V  G=1.164*Y-0.213*U-0.533*V  B=1.164*Y+2.112*U
                const int c = 298 * Y + 128;
                dst[col] = qRgb(qBound(0, (c + 459 * V) >> 8, 255),
                    qBound(0, (c - 55 * U - 136 * V) >> 8, 255),
                    qBound(0, (c + 541 * U) >> 8, 255));
            }
        }
        return out;
    }

    // YUY2 (packed 4:2:2) → QImage(Format_RGB32), stride in bytes.
    // Coefficients: BT.709.
    QImage yuy2ToRgb32(const uchar* yuy2, int w, int h, int stride)
    {
        QImage out(w, h, QImage::Format_RGB32);
        for (int row = 0; row < h; ++row) {
            const uchar* src = yuy2 + row * stride;
            QRgb* dst = reinterpret_cast<QRgb*>(out.scanLine(row));
            for (int col = 0; col < w; col += 2) {
                const int Y0 = static_cast<int>(src[col * 2]) - 16;
                const int U = static_cast<int>(src[col * 2 + 1]) - 128;
                const int Y1 = static_cast<int>(src[col * 2 + 2]) - 16;
                const int V = static_cast<int>(src[col * 2 + 3]) - 128;
                // BT.709
                const int c0 = 298 * Y0 + 128;
                const int c1 = 298 * Y1 + 128;
                dst[col] = qRgb(qBound(0, (c0 + 459 * V) >> 8, 255),
                    qBound(0, (c0 - 55 * U - 136 * V) >> 8, 255),
                    qBound(0, (c0 + 541 * U) >> 8, 255));
                if (col + 1 < w)
                    dst[col + 1] = qRgb(qBound(0, (c1 + 459 * V) >> 8, 255),
                        qBound(0, (c1 - 55 * U - 136 * V) >> 8, 255),
                        qBound(0, (c1 + 541 * U) >> 8, 255));
            }
        }
        return out;
    }

} // anonymous namespace

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

VideoDecoder::VideoDecoder(QObject* parent)
    : QObject(parent)
{
}

VideoDecoder::~VideoDecoder()
{
    stop();
}

// ---------------------------------------------------------------------------
// Control
// ---------------------------------------------------------------------------

bool VideoDecoder::start()
{
    if (!m_running.testAndSetAcquire(0, 1)) {
        return true; // already running
    }

    // Launch dedicated decode thread
    m_thread = new QThread(this);
    m_thread->setObjectName(QStringLiteral("DecodeThread"));
    QObject::connect(m_thread, &QThread::started,
        this, &VideoDecoder::decodeLoop,
        Qt::DirectConnection);
    QObject::connect(m_thread, &QThread::finished,
        m_thread, &QObject::deleteLater);
    m_thread->start();
    return true;
}

void VideoDecoder::stop()
{
    if (!m_running.testAndSetAcquire(1, 0)) {
        return; // not running
    }

    // Wake decode thread so it can see m_running == 0 and exit
    {
        QMutexLocker lock(&m_queueMutex);
        m_queueCond.wakeAll();
    }

    if (m_thread) {
        m_thread->wait(2000);
        // m_thread is deleteLater-connected to finished; null the pointer only
        m_thread = nullptr;
    }
}

bool VideoDecoder::isRunning() const
{
    return m_running.loadAcquire() != 0;
}

// ---------------------------------------------------------------------------
// Public: enqueue packet (non-blocking, called from any thread)
// ---------------------------------------------------------------------------

void VideoDecoder::decodePacket(const QByteArray& nalData, bool isKeyframe)
{
    if (!m_running.loadAcquire()) { return; }

    QMutexLocker lock(&m_queueMutex);
    // Keep queue bounded to prevent runaway memory on a slow decoder.
    // Drop oldest non-keyframe when queue exceeds 4 entries.
    if (m_queue.size() >= 4) {
        for (int i = 0; i < m_queue.size(); ++i) {
            if (!m_queue[i].isKeyframe) {
                m_queue.removeAt(i);
                break;
            }
        }
    }
    m_queue.enqueue({ nalData, isKeyframe });
    m_queueCond.wakeOne();
}

// ---------------------------------------------------------------------------
// Decode loop — runs exclusively on m_thread
// ---------------------------------------------------------------------------

void VideoDecoder::decodeLoop()
{
    if (!initDecoder()) {
        m_running.storeRelease(0);
        emit decodeError(QStringLiteral("Failed to initialise H.264 decoder"));
        return;
    }

    while (m_running.loadAcquire()) {
        Packet pkt;
        {
            QMutexLocker lock(&m_queueMutex);
            while (m_queue.isEmpty() && m_running.loadAcquire()) {
                m_queueCond.wait(&m_queueMutex, 50 /*ms*/);
            }
            if (m_queue.isEmpty()) { break; }
            pkt = m_queue.dequeue();
        }

        if (feedPacket(pkt)) {
            drainOutput();
        }
    }

    shutdownDecoder();
}

// ---------------------------------------------------------------------------
// MF initialisation / teardown
// ---------------------------------------------------------------------------

bool VideoDecoder::initDecoder()
{
    HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET);
    if (FAILED(hr)) {
        qWarning() << "VideoDecoder: MFStartup failed" << hrStr(hr);
        return false;
    }

    MFT_REGISTER_TYPE_INFO inputType = { MFMediaType_Video, MFVideoFormat_H264 };
    IMFActivate** ppActivate = nullptr;
    UINT32 count = 0;
    // Prefer hardware, accept software as fallback
    hr = MFTEnumEx(MFT_CATEGORY_VIDEO_DECODER,
        MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_ASYNCMFT |
        MFT_ENUM_FLAG_HARDWARE | MFT_ENUM_FLAG_SORTANDFILTER,
        &inputType, nullptr, &ppActivate, &count);
    if (FAILED(hr) || count == 0) {
        qWarning() << "VideoDecoder: no H.264 decoder MFT found";
        MFShutdown();
        return false;
    }

    ComPtr<IMFTransform> decoder;
    hr = ppActivate[0]->ActivateObject(IID_PPV_ARGS(&decoder));
    for (UINT32 i = 0; i < count; ++i) ppActivate[i]->Release();
    CoTaskMemFree(ppActivate);
    if (FAILED(hr)) { MFShutdown(); return false; }

    // Input: H.264
    ComPtr<IMFMediaType> inType;
    MFCreateMediaType(&inType);
    inType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    inType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    MFSetAttributeSize(inType.Get(), MF_MT_FRAME_SIZE, 1920, 1080);
    MFSetAttributeRatio(inType.Get(), MF_MT_FRAME_RATE, 30, 1);
    hr = decoder->SetInputType(0, inType.Get(), 0);
    if (FAILED(hr)) { MFShutdown(); return false; }

    // Output: NV12 (prefer) → YUY2 fallback
    ComPtr<IMFMediaType> outType;
    MFCreateMediaType(&outType);
    outType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    outType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
    MFSetAttributeSize(outType.Get(), MF_MT_FRAME_SIZE, 1920, 1080);
    MFSetAttributeRatio(outType.Get(), MF_MT_FRAME_RATE, 30, 1);
    hr = decoder->SetOutputType(0, outType.Get(), 0);
    if (FAILED(hr)) {
        outType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_YUY2);
        hr = decoder->SetOutputType(0, outType.Get(), 0);
    }
    if (FAILED(hr)) { MFShutdown(); return false; }

    decoder->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
    decoder->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
    decoder->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);

    m_decoder = decoder.Detach();
    m_initialized = true;
    return true;
}

void VideoDecoder::shutdownDecoder()
{
    if (m_decoder) {
        m_decoder->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
        m_decoder->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, 0);
        m_decoder->Release();
        m_decoder = nullptr;
    }
    m_initialized = false;
    MFShutdown();
}

// ---------------------------------------------------------------------------
// Feed one packet into the MFT (called on decode thread)
// ---------------------------------------------------------------------------

bool VideoDecoder::feedPacket(const Packet& pkt)
{
    const LONGLONG duration = 10'000'000LL / 30; // ~30 fps, 100-ns units
    ComPtr<IMFSample> sample;
    HRESULT hr = wrapInSample(
        reinterpret_cast<const BYTE*>(pkt.data.constData()),
        static_cast<DWORD>(pkt.data.size()),
        m_pts, duration, &sample);
    m_pts += duration;

    if (FAILED(hr)) { return false; }

    hr = m_decoder->ProcessInput(0, sample.Get(), 0);
    return SUCCEEDED(hr) || hr == MF_E_NOTACCEPTING;
}

// ---------------------------------------------------------------------------
// Drain all available decoded frames (called on decode thread)
// ---------------------------------------------------------------------------

void VideoDecoder::drainOutput()
{
    MFT_OUTPUT_STREAM_INFO streamInfo{};
    m_decoder->GetOutputStreamInfo(0, &streamInfo);
    const bool decoderAllocates =
        (streamInfo.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES) != 0;
    const DWORD bufSize = streamInfo.cbSize ? streamInfo.cbSize : 1920 * 1080 * 4;

    for (;;) {
        MFT_OUTPUT_DATA_BUFFER outputBuf{};
        ComPtr<IMFSample> outSample;

        if (!decoderAllocates) {
            ComPtr<IMFMediaBuffer> outBuf;
            if (FAILED(MFCreateMemoryBuffer(bufSize, &outBuf))) { break; }
            MFCreateSample(&outSample);
            outSample->AddBuffer(outBuf.Get());
            outputBuf.pSample = outSample.Get();
        }

        DWORD status = 0;
        HRESULT hr = m_decoder->ProcessOutput(0, 1, &outputBuf, &status);
        if (outputBuf.pEvents) { outputBuf.pEvents->Release(); outputBuf.pEvents = nullptr; }
        if (decoderAllocates && outputBuf.pSample) {
            outSample.Attach(outputBuf.pSample);
        }

        if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT) { break; }
        if (FAILED(hr)) { break; }
        if (!outSample) { continue; }

        // Read dimensions + format once on the first frame.
        if (m_width == 0 || m_height == 0) {
            ComPtr<IMFMediaType> outMediaType;
            if (SUCCEEDED(m_decoder->GetOutputCurrentType(0, &outMediaType))) {
                UINT32 w = 1920, h = 1080;
                MFGetAttributeSize(outMediaType.Get(), MF_MT_FRAME_SIZE, &w, &h);
                m_width = static_cast<int>(w);
                m_height = static_cast<int>(h);

                GUID subtype = {};
                outMediaType->GetGUID(MF_MT_SUBTYPE, &subtype);
                m_isYUY2 = (subtype == MFVideoFormat_YUY2);
            }
        }

        const int w = m_width ? m_width : 1920;
        const int h = m_height ? m_height : 1080;

        // ----------------------------------------------------------------
   // Use IMF2DBuffer::Lock2D to get the real hardware row stride.
  // This is the only reliable way to obtain the correct pitch for
        // both D3D-backed surfaces (hardware decoder) and system-memory
  // buffers (software decoder).  Fall back to ConvertToContiguous
        // only if the buffer does not implement IMF2DBuffer.
        // ----------------------------------------------------------------
        QImage frame;

        ComPtr<IMFMediaBuffer> firstBuf;
        if (FAILED(outSample->GetBufferByIndex(0, &firstBuf))) { continue; }

        ComPtr<IMF2DBuffer> buf2d;
        if (SUCCEEDED(firstBuf.As(&buf2d))) {
            // Hardware path: Lock2D returns the real stride.
            BYTE* scanline0 = nullptr;
            LONG  pitch = 0;
            if (FAILED(buf2d->Lock2D(&scanline0, &pitch))) { continue; }

            const int stride = static_cast<int>(pitch < 0 ? -pitch : pitch);
            const uchar* data = reinterpret_cast<const uchar*>(
                pitch < 0
                ? scanline0 + pitch * (h - 1)  // bottom-up: rewind to row 0
                : scanline0);

            frame = m_isYUY2
                ? yuy2ToRgb32(data, w, h, stride)
                : nv12ToRgb32(data, w, h, stride);

            buf2d->Unlock2D();
        }
        else {
            // Software / system-memory path: buffer is already packed.
            ComPtr<IMFMediaBuffer> contiguous;
            if (FAILED(outSample->ConvertToContiguousBuffer(&contiguous))) { continue; }
            BYTE* rawData = nullptr; DWORD curLen = 0;
            if (FAILED(contiguous->Lock(&rawData, nullptr, &curLen))) { continue; }

            // YUY2: 2 bytes per pixel (packed 4:2:2), stride = w*2.
                // NV12: 1 byte per pixel for luma plane, stride = w.
            const int stride = m_isYUY2 ? w * 2 : w;
            const uchar* data = reinterpret_cast<const uchar*>(rawData);
            frame = m_isYUY2
                ? yuy2ToRgb32(data, w, h, stride)
                : nv12ToRgb32(data, w, h, stride);

            contiguous->Unlock();
        }

        if (!frame.isNull()) {
            emit frameReady(frame);
        }
    }
}
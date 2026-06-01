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
#include <wrl/client.h>
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfuuid.lib")

#include <QDebug>
#include <QMutexLocker>
#include <QMetaObject>
#include <QCoreApplication>
#include <QThread>

using Microsoft::WRL::ComPtr;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

    QString hrStr(HRESULT hr)
    {
        return QStringLiteral("HRESULT 0x") +
            QString::number(static_cast<unsigned long>(hr), 16).toUpper();
    }

    /// Wrap a raw byte buffer in an IMFMediaBuffer / IMFSample.
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
        hr = sample->AddBuffer(buf.Get());
        if (FAILED(hr)) return hr;
        sample->SetSampleTime(timestamp);
        sample->SetSampleDuration(duration);

        *ppSample = sample.Detach();
        return S_OK;
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
    QMutexLocker lock(&m_mutex);
    if (m_running.loadAcquire()) {
        return true; // already running
    }

    HRESULT hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) {
        qWarning() << "VideoDecoder: MFStartup failed" << hrStr(hr);
        return false;
    }

    // Find the H.264 decoder MFT.
    MFT_REGISTER_TYPE_INFO inputType = { MFMediaType_Video, MFVideoFormat_H264 };
    IMFActivate** ppActivate = nullptr;
    UINT32 count = 0;
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

    if (FAILED(hr)) {
        qWarning() << "VideoDecoder: ActivateObject failed" << hrStr(hr);
        MFShutdown();
        return false;
    }

    // Set input media type: H.264.
    ComPtr<IMFMediaType> inType;
    MFCreateMediaType(&inType);
    inType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    inType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    // Provide a hint; decoder will update on first keyframe.
    MFSetAttributeSize(inType.Get(), MF_MT_FRAME_SIZE, 1920, 1080);
    MFSetAttributeRatio(inType.Get(), MF_MT_FRAME_RATE, 30, 1);
    hr = decoder->SetInputType(0, inType.Get(), 0);
    if (FAILED(hr)) {
        qWarning() << "VideoDecoder: SetInputType failed" << hrStr(hr);
        MFShutdown();
        return false;
    }

    // Set output media type: NV12 (most HW decoders prefer NV12; we convert).
    ComPtr<IMFMediaType> outType;
    MFCreateMediaType(&outType);
    outType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    outType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
    MFSetAttributeSize(outType.Get(), MF_MT_FRAME_SIZE, 1920, 1080);
    MFSetAttributeRatio(outType.Get(), MF_MT_FRAME_RATE, 30, 1);
    hr = decoder->SetOutputType(0, outType.Get(), 0);
    if (FAILED(hr)) {
        // Fallback: try YUY2
        outType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_YUY2);
        hr = decoder->SetOutputType(0, outType.Get(), 0);
    }
    if (FAILED(hr)) {
        qWarning() << "VideoDecoder: SetOutputType failed" << hrStr(hr);
        MFShutdown();
        return false;
    }

    hr = decoder->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
    hr = decoder->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
    hr = decoder->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);

    m_decoder = decoder.Detach();
    m_initialized = true;
    m_running.storeRelease(1);
    return true;
}

void VideoDecoder::stop()
{
    QMutexLocker lock(&m_mutex);
    if (!m_running.loadAcquire()) { return; }
    m_running.storeRelease(0);

    if (m_decoder) {
        m_decoder->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
        m_decoder->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, 0);
        m_decoder->Release();
        m_decoder = nullptr;
    }
    m_initialized = false;
    MFShutdown();
}

bool VideoDecoder::isRunning() const
{
    return m_running.loadAcquire() != 0;
}

// ---------------------------------------------------------------------------
// Packet input
// ---------------------------------------------------------------------------

void VideoDecoder::decodePacket(const QByteArray& nalData, bool /*isKeyframe*/)
{
    if (!m_running.loadAcquire()) { return; }

    QMutexLocker lock(&m_mutex);
    if (!m_decoder) { return; }

    if (!feedPacket(nalData, false)) { return; }
    drainOutput();
}

// ---------------------------------------------------------------------------
// Private – feed one packet into the MFT
// ---------------------------------------------------------------------------

bool VideoDecoder::feedPacket(const QByteArray& nalData, bool /*isKeyframe*/)
{
    static LONGLONG s_timestamp = 0;
    const LONGLONG  duration = 333333; // ~30 fps in 100-ns units

    ComPtr<IMFSample> sample;
    HRESULT hr = wrapInSample(
        reinterpret_cast<const BYTE*>(nalData.constData()),
        static_cast<DWORD>(nalData.size()),
        s_timestamp, duration, &sample);
    s_timestamp += duration;

    if (FAILED(hr)) {
        qWarning() << "VideoDecoder: wrapInSample failed" << hrStr(hr);
        return false;
    }

    hr = m_decoder->ProcessInput(0, sample.Get(), 0);
    if (FAILED(hr) && hr != MF_E_NOTACCEPTING) {
        qWarning() << "VideoDecoder: ProcessInput failed" << hrStr(hr);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Private – drain all available output frames
// ---------------------------------------------------------------------------

bool VideoDecoder::drainOutput()
{
    // Query the output stream info to know whether to allocate the buffer.
    MFT_OUTPUT_STREAM_INFO streamInfo{};
    m_decoder->GetOutputStreamInfo(0, &streamInfo);
    const bool decoderAllocates =
        (streamInfo.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES) != 0;

    for (;;) {
        MFT_OUTPUT_DATA_BUFFER outputBuf{};
        ComPtr<IMFSample> outSample;

        if (!decoderAllocates) {
            // Allocate output buffer manually.
            ComPtr<IMFMediaBuffer> outBuf;
            HRESULT hr2 = MFCreateMemoryBuffer(streamInfo.cbSize
                ? streamInfo.cbSize : 1920 * 1080 * 2, &outBuf);
            if (FAILED(hr2)) { break; }
            MFCreateSample(&outSample);
            outSample->AddBuffer(outBuf.Get());
            outputBuf.pSample = outSample.Get();
        }

        DWORD status = 0;
        HRESULT hr = m_decoder->ProcessOutput(0, 1, &outputBuf, &status);

        if (outputBuf.pEvents) {
            outputBuf.pEvents->Release();
            outputBuf.pEvents = nullptr;
        }
        if (decoderAllocates && outputBuf.pSample) {
            outSample.Attach(outputBuf.pSample);
        }

        if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT) {
            break; // need more packets
        }
        if (FAILED(hr)) {
            break;
        }
        if (!outSample) { continue; }

        // --- Read decoded NV12 (or YUY2) frame and convert to QImage ---
        ComPtr<IMFMediaBuffer> decodedBuf;
        if (FAILED(outSample->ConvertToContiguousBuffer(&decodedBuf))) { continue; }

        // Determine actual output dimensions from media type.
        ComPtr<IMFMediaType> outMediaType;
        UINT32 w = 1920, h = 1080;
        if (SUCCEEDED(m_decoder->GetOutputCurrentType(0, &outMediaType))) {
            MFGetAttributeSize(outMediaType.Get(), MF_MT_FRAME_SIZE, &w, &h);
            GUID subtype = GUID_NULL;
            outMediaType->GetGUID(MF_MT_SUBTYPE, &subtype);
        }

        BYTE* rawData = nullptr;
        DWORD  maxLen = 0;
        DWORD  curLen = 0;
        if (FAILED(decodedBuf->Lock(&rawData, &maxLen, &curLen))) { continue; }

        // Convert NV12 → RGB888 (simple but correct implementation).
        QImage frame(static_cast<int>(w), static_cast<int>(h), QImage::Format_RGB888);
        const int iw = static_cast<int>(w);
        const int ih = static_cast<int>(h);

        const uchar* yPlane = rawData;
        const uchar* uvPlane = rawData + iw * ih;

        for (int row = 0; row < ih; ++row) {
            uchar* dst = frame.scanLine(row);
            for (int col = 0; col < iw; ++col) {
                const int Y = yPlane[row * iw + col];
                const int uvIdx = (row / 2) * iw + (col & ~1);
                const int U = uvPlane[uvIdx] - 128;
                const int V = uvPlane[uvIdx + 1] - 128;

                // BT.601 full-swing
                const int r = qBound(0, (298 * (Y - 16) + 409 * V + 128) >> 8, 255);
                const int g = qBound(0, (298 * (Y - 16) - 100 * U - 208 * V + 128) >> 8, 255);
                const int b = qBound(0, (298 * (Y - 16) + 516 * U + 128) >> 8, 255);

                dst[col * 3 + 0] = static_cast<uchar>(r);
                dst[col * 3 + 1] = static_cast<uchar>(g);
                dst[col * 3 + 2] = static_cast<uchar>(b);
            }
        }
        decodedBuf->Unlock();

        // Emit on main thread.
        const QImage captured = frame;
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [this, captured]() { emit frameReady(captured); },
            Qt::QueuedConnection);
    }
    return true;
}
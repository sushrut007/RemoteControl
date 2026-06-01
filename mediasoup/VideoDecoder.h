#pragma once

#include <QObject>
#include <QImage>
#include <QByteArray>
#include <QMutex>
#include <QAtomicInt>

// Forward declare MF types to keep Windows headers out of the public interface.
struct IMFTransform;
struct IMFSample;

// ---------------------------------------------------------------------------
// VideoDecoder
//
// Decodes an H.264 Annex-B / NAL-unit bitstream into QImage frames using the
// Windows Media Foundation H.264 video decoder (MFT).
//
// Usage
// -----
//  1. Construct on any thread; all MF calls are confined to a single internal
//     thread to satisfy MTA/STA requirements.
//  2. Call start() once before feeding data.
//  3. Feed packets with decodePacket().  frameReady() fires (via queued
//     connection) when a decoded frame is available.
//  4. Call stop() to release all MF resources.
// ---------------------------------------------------------------------------
class VideoDecoder : public QObject
{
    Q_OBJECT

public:
    explicit VideoDecoder(QObject* parent = nullptr);
    ~VideoDecoder() override;

    VideoDecoder(const VideoDecoder&) = delete;
    VideoDecoder& operator=(const VideoDecoder&) = delete;

    // -----------------------------------------------------------------------
    // Control
    // -----------------------------------------------------------------------

    /// Initialise the MF H.264 decoder.  Safe to call from any thread.
    bool start();

    /// Release MF resources.  Safe to call from any thread.
    void stop();

    bool isRunning() const;

    // -----------------------------------------------------------------------
    // Input
    // -----------------------------------------------------------------------

    /// Feed one encoded H.264 NAL packet (Annex-B or raw NAL, base64-decoded).
    /// Safe to call from any thread.
    void decodePacket(const QByteArray& nalData, bool isKeyframe);

signals:
    /// Emitted on the main thread each time a frame has been decoded.
    void frameReady(const QImage& frame);

    /// Emitted on the main thread when a decode error occurs.
    void decodeError(const QString& message);

private:
    void initMF();
    void shutdownMF();
    bool feedPacket(const QByteArray& nalData, bool isKeyframe);
    bool drainOutput();

    IMFTransform* m_decoder{ nullptr };
    int           m_width{ 0 };
    int           m_height{ 0 };
    bool          m_initialized{ false };
    QAtomicInt    m_running{ 0 };
    QMutex        m_mutex;
};
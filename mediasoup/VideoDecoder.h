#pragma once

#include <QObject>
#include <QImage>
#include <QByteArray>
#include <QMutex>
#include <QWaitCondition>
#include <QAtomicInt>
#include <QThread>
#include <QQueue>

// Forward declare MF types to keep Windows headers out of the public interface.
struct IMFTransform;

// ---------------------------------------------------------------------------
// VideoDecoder
//
// Decodes an H.264 NAL bitstream into QImage frames using the Windows Media
// Foundation H.264 MFT on a dedicated background thread.
//
// Usage
// -----
//  1. Call start() once.
//  2. Feed packets with decodePacket() from any thread (non-blocking enqueue).
//  3. frameReady() is emitted via queued connection to the thread where the
//   VideoDecoder lives.
//  4. Call stop() to drain and release all MF resources.
// ---------------------------------------------------------------------------
class VideoDecoder : public QObject
{
    Q_OBJECT

public:
    explicit VideoDecoder(QObject* parent = nullptr);
    ~VideoDecoder() override;

    VideoDecoder(const VideoDecoder&) = delete;
    VideoDecoder& operator=(const VideoDecoder&) = delete;

    bool start();
    void stop();
    bool isRunning() const;

    /// Non-blocking: enqueues the packet for background decoding.
    void decodePacket(const QByteArray& nalData, bool isKeyframe);

signals:
    void frameReady(const QImage& frame);
    void decodeError(const QString& message);

private:
    // -----------------------------------------------------------------------
    // Packet queue
 // -----------------------------------------------------------------------
    struct Packet { QByteArray data; bool isKeyframe; };

    void decodeLoop();           ///< Runs on m_thread
    bool initDecoder();
    void shutdownDecoder();
    bool feedPacket(const Packet& pkt);
    void drainOutput();

    // -----------------------------------------------------------------------
  // Members
    // -----------------------------------------------------------------------
    QThread* m_thread{ nullptr };

    IMFTransform* m_decoder{ nullptr };
    bool          m_initialized{ false };
    QAtomicInt    m_running{ 0 };

    // Queue shared between caller thread and decode thread
    QMutex  m_queueMutex;
    QWaitCondition m_queueCond;
    QQueue<Packet> m_queue;

    // Cached decoded frame dimensions
    int  m_width{ 0 };
    int  m_height{ 0 };
    int  m_stride{ 0 };  ///< Decoder row pitch in bytes (hardware-aligned, >= m_width)
    bool m_isYUY2{ false }; ///< true when decoder chose YUY2 instead of NV12

    // Presentation timestamp counter (100-ns units, used on decode thread only)
    long long m_pts{ 0 };
};
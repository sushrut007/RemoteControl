#pragma once

#include <QObject>
#include <QImage>
#include <QAtomicInt>
#include <QString>
#include <QElapsedTimer>
#include <QMutex>

#include <atomic>
#include <cstdint>
#include "../core/AppState.h"

class VideoProducer : public QObject
{
    Q_OBJECT

public:
    explicit VideoProducer(QObject* parent = nullptr);
    ~VideoProducer() override;

    VideoProducer(const VideoProducer&) = delete;
    VideoProducer& operator=(const VideoProducer&) = delete;

    // -----------------------------------------------------------------------
    // Control
    // -----------------------------------------------------------------------

    void start(int width, int height, int fps, int bitrateKbps);
    void stop();

    // -----------------------------------------------------------------------
    // Frame input
    // -----------------------------------------------------------------------

    void onFrame(const QImage& frame);

    // -----------------------------------------------------------------------
    // Bitrate adaptation
    // -----------------------------------------------------------------------

    void notifyRtt(int rttMs);

    // -----------------------------------------------------------------------
    // Keyframe control
    // -----------------------------------------------------------------------

    void forceKeyframe();

    // -----------------------------------------------------------------------
    // Accessors
    // -----------------------------------------------------------------------

    bool isRunning() const;

    // -----------------------------------------------------------------------
    // Called by EncodeTask (QRunnable) – must be public
    // -----------------------------------------------------------------------

    /// Encode one frame.  Called from QThreadPool.
    QByteArray encodeFrame(const QImage& frame, bool forceKey);

    /// Update adaptive bitrate state.  Called after each encode.
    void updateAbrState();

    QAtomicInt m_encodingInFlight{ 0 };  ///< 1 = a QThreadPool task is running

signals:
    // -----------------------------------------------------------------------
    // Qt signals
    // -----------------------------------------------------------------------

    void packetReady(const QByteArray& data, bool isKeyframe);
    void encodingError(const QString& message);

    /// Emitted roughly once per second with current encoder statistics.
    void statsUpdated(const VideoStats& stats);

private:
    // -----------------------------------------------------------------------
    // Internal encoding helpers
    // -----------------------------------------------------------------------

    bool initMfH264(int width, int height, int fps, int bitrateKbps);
    bool initVp8Fallback(int width, int height, int fps, int bitrateKbps);
    void releaseEncoder();

    QByteArray encodeMfFrame(const QImage& frame, bool forceKey);
    QByteArray encodeVp8Frame(const QImage& frame, bool forceKey);

    void applyBitrate(int kbps);

    // -----------------------------------------------------------------------
    // Encoder state
    // -----------------------------------------------------------------------

    enum class Codec { None, H264_MF, VP8_SW };

    Codec    m_codec{ Codec::None };
    bool     m_hardwareEncoding{ false };

    void* m_mfTransform{ nullptr };
    void* m_mfInputType{ nullptr };
    void* m_mfOutputType{ nullptr };
    void* m_vpxCodec{ nullptr };

    // -----------------------------------------------------------------------
    // Configuration
    // -----------------------------------------------------------------------

    int m_width{ 0 };
    int m_height{ 0 };
    int m_fps{ 30 };
    int m_bitrateKbps{ 1000 };
    int m_maxBitrateKbps{ 1000 };

    // -----------------------------------------------------------------------
    // Adaptive bitrate
    // -----------------------------------------------------------------------

    std::atomic<int>  m_currentRttMs{ 0 };
    QElapsedTimer     m_abrTimer;
    int               m_abrCooldownMs{ 1000 };

    // -----------------------------------------------------------------------
    // Thread safety
    // -----------------------------------------------------------------------

    QAtomicInt m_running{ 0 };
    QAtomicInt m_forceKeyframe{ 0 };

    mutable QMutex m_encoderMutex;

    // -----------------------------------------------------------------------
    // Statistics
    // -----------------------------------------------------------------------

    std::atomic<int>  m_framesEncoded{ 0 };
    QElapsedTimer     m_statsTimer;
    int               m_framesSinceLastStat{ 0 };
};
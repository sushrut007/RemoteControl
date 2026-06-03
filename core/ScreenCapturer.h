#pragma once

#include <QObject>
#include <QImage>
#include <QAtomicInt>
#include <QString>

#include <memory>

// Forward declarations – keep Windows headers out of the public interface.
struct CaptureWorker;
QT_FORWARD_DECLARE_CLASS(QThread)

// ---------------------------------------------------------------------------
// ScreenCapturer
//
// Captures the desktop on Windows using the DXGI Desktop Duplication API
// (IDXGIOutputDuplication) as the primary path, with a GDI BitBlt fallback
// when DXGI is unavailable (e.g. on RDP sessions or older hardware).
//
// Architecture
// ------------
//  - A dedicated QThread runs the capture loop so the caller's thread is
//    never blocked.
//  - CaptureWorker (internal, QObject-on-thread) owns all DXGI/GDI resources.
//    Its lifetime is strictly bounded by the worker thread.
//  - start() / stop() are safe to call from any thread.
//  - Qt signals are delivered to connected slots in their respective threads
//    via the normal queued-connection mechanism.
//
// Frame delta detection
// ---------------------
//  A CRC-32 of the raw pixel data is computed after each capture. If the
//  checksum matches the previous frame the signal is suppressed, avoiding
//  unnecessary copies and downstream processing when the screen is idle.
// ---------------------------------------------------------------------------
class ScreenCapturer : public QObject
{
    Q_OBJECT

public:
    explicit ScreenCapturer(QObject* parent = nullptr);
    ~ScreenCapturer() override;

    ScreenCapturer(const ScreenCapturer&) = delete;
    ScreenCapturer& operator=(const ScreenCapturer&) = delete;

    // -----------------------------------------------------------------------
    // Control
    // -----------------------------------------------------------------------

    /// Start capturing on the given monitor at the requested frame rate.
    /// Safe to call from any thread.  Returns false immediately if a capture
    /// session is already running; call stop() first to switch monitors.
    /// @param monitorIndex  0-based index into the DXGI adapter output list.
    /// @param targetFps     Desired capture rate (1–120).  Actual rate is
    ///                      capped by how fast the OS can supply frames.
    bool start(int monitorIndex = 0, int targetFps = 30);

    /// Stop the capture loop and release all GPU/GDI resources.
    /// Blocks until the worker thread has exited (max ~500 ms).
    /// Safe to call from any thread, including when already stopped.
    void stop();

    /// Set the JPEG quality used when the frame is compressed before sending
    /// over the network.  Does not affect the QImage emitted by frameReady().
    /// @param quality  Clamped to [60, 90].
    void setQuality(int quality);

    /// Returns true if a capture session is currently active.
    bool isRunning() const;

signals:
    // -----------------------------------------------------------------------
    // Qt signals  (delivered on caller's thread via queued connection)
    // -----------------------------------------------------------------------

    /// Emitted for every unique frame captured (delta-detected duplicates are
    /// suppressed).  The QImage format is QImage::Format_ARGB32 (BGRA byte
    /// order on little-endian Windows).
    void frameReady(const QImage& frame);

    /// Emitted once after DXGI initialises, providing the captured monitor's
    /// position and size in virtual-desktop coordinates.
    /// Connect to InputInjector::setCaptureRegion() to enable accurate
    /// multi-monitor coordinate mapping.
    void captureRegionReady(int originX, int originY, int width, int height);

    /// Emitted when an unrecoverable capture error occurs.  The capture loop
    /// stops automatically before this signal is emitted.
    void captureError(const QString& message);

private:
    QThread* m_thread{ nullptr };
    CaptureWorker* m_worker{ nullptr };  ///< Lives on m_thread

    QAtomicInt m_running{ 0 };  ///< 1 = active, 0 = stopped
    int        m_jpegQuality{ 75 };
};
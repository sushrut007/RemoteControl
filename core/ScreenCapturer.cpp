#include "ScreenCapturer.h"

#include <QThread>
#include <QTimer>
#include <QImage>
#include <QElapsedTimer>

// Windows / DXGI headers – only in the .cpp
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <windows.h>
#include <dxgi1_2.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <cstdint>

using Microsoft::WRL::ComPtr;

// ---------------------------------------------------------------------------
// GDI fallback helper
// ---------------------------------------------------------------------------

namespace {

    /// Capture the entire virtual screen via GDI BitBlt into a QImage.
    /// Returns a null QImage on failure.
    QImage captureGdi(int /*monitorIndex*/)
    {
        const int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
        const int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
        const int w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        const int h = GetSystemMetrics(SM_CYVIRTUALSCREEN);

        if (w <= 0 || h <= 0) {
            return {};
        }

        HDC screenDc = GetDC(nullptr);
        if (!screenDc) {
            return {};
        }

        HDC memDc = CreateCompatibleDC(screenDc);
        if (!memDc) {
            ReleaseDC(nullptr, screenDc);
            return {};
        }

        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = w;
        bmi.bmiHeader.biHeight = -h; // top-down
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        void* bits = nullptr;
        HBITMAP bmp = CreateDIBSection(memDc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!bmp || !bits) {
            DeleteDC(memDc);
            ReleaseDC(nullptr, screenDc);
            return {};
        }

        HGDIOBJ oldBmp = SelectObject(memDc, bmp);
        const bool ok = BitBlt(memDc, 0, 0, w, h, screenDc, x, y, SRCCOPY | CAPTUREBLT);

        QImage result;
        if (ok) {
            // QImage takes a shallow copy of 'bits'; we must keep 'bmp' alive
            // until the deep copy is done.
            result = QImage(static_cast<const uchar*>(bits),
                w, h,
                w * 4,
                QImage::Format_RGB32).copy(); // deep-copy before freeing
        }

        SelectObject(memDc, oldBmp);
        DeleteObject(bmp);
        DeleteDC(memDc);
        ReleaseDC(nullptr, screenDc);

        return result;
    }

} // anonymous namespace

// ---------------------------------------------------------------------------
// CaptureWorker  – lives on the capture thread
// ---------------------------------------------------------------------------

struct CaptureWorker : public QObject
{
    Q_OBJECT

public:
    explicit CaptureWorker(QObject* parent = nullptr) : QObject(parent) {}

    // Configuration (set before moveToThread / start())
    int monitorIndex{ 0 };
    int targetFps{ 30 };
    QAtomicInt* runningFlag{ nullptr }; ///< Shared with ScreenCapturer

signals:
    void frameReady(const QImage& frame);
    void captureError(const QString& message);
    void captureRegionReady(int originX, int originY, int width, int height);

public slots:
    void run()
    {
        // Elevate the capture thread so the OS does not throttle it when the
        // host application is minimised or not in the foreground.
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

        const bool dxgiOk = initDxgi();
        if (!dxgiOk) {
            // Fall back to GDI; not a fatal error.
            m_useDxgi = false;
        }
        else {
            emit captureRegionReady(m_originX, m_originY, m_width, m_height);
        }

        const int frameIntervalMs = (targetFps > 0)
            ? (1000 / qBound(1, targetFps, 120))
            : 33;

        QElapsedTimer frameTimer;
        frameTimer.start();

        while (runningFlag && runningFlag->loadAcquire() == 1) {
            const qint64 t0 = frameTimer.elapsed();

            QImage frame = m_useDxgi ? captureDxgi() : captureGdi(monitorIndex);

            if (frame.isNull()) {
                if (m_useDxgi) {
                    // DXGI frame unavailable this tick (e.g. timeout) – not an
                    // error; just skip and try next iteration.
                }
                else {
                    emit captureError(QStringLiteral("GDI capture failed"));
                    break;
                }
            }
            else {
                // DXGI only delivers a new frame when the desktop actually
                // changes (AcquireNextFrame returns WAIT_TIMEOUT otherwise),
                // so the old per-frame CRC-32 delta check is redundant and
                // was the primary bottleneck (~8MB hash per frame at 1080p).
                // For GDI we still emit every frame since GDI always succeeds.
                emit frameReady(frame);
            }

            // Throttle to targetFps
            const qint64 elapsed = frameTimer.elapsed() - t0;
            const qint64 sleepMs = frameIntervalMs - elapsed;
            if (sleepMs > 0) {
                QThread::msleep(static_cast<unsigned long>(sleepMs));
            }
        }

        releaseDxgi();
    }

private:
    // -----------------------------------------------------------------------
    // DXGI Desktop Duplication
    // -----------------------------------------------------------------------

    bool initDxgi()
    {
        HRESULT hr = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            0,
            nullptr, 0,
            D3D11_SDK_VERSION,
            &m_d3dDevice,
            nullptr,
            &m_d3dContext);

        if (FAILED(hr)) {
            return false;
        }

        ComPtr<IDXGIDevice> dxgiDevice;
        hr = m_d3dDevice.As(&dxgiDevice);
        if (FAILED(hr)) { return false; }

        ComPtr<IDXGIAdapter> adapter;
        hr = dxgiDevice->GetAdapter(&adapter);
        if (FAILED(hr)) { return false; }

        // Enumerate outputs and pick the requested monitor index.
        ComPtr<IDXGIOutput> output;
        hr = adapter->EnumOutputs(static_cast<UINT>(monitorIndex), &output);
        if (FAILED(hr)) {
            // Fall back to monitor 0.
            hr = adapter->EnumOutputs(0, &output);
            if (FAILED(hr)) { return false; }
        }

        ComPtr<IDXGIOutput1> output1;
        hr = output.As(&output1);
        if (FAILED(hr)) { return false; }

        hr = output1->DuplicateOutput(m_d3dDevice.Get(), &m_duplication);
        if (FAILED(hr)) { return false; }

        // Query output dimensions for the staging texture.
        DXGI_OUTPUT_DESC desc{};
        output->GetDesc(&desc);
        m_originX = static_cast<int>(desc.DesktopCoordinates.left);
        m_originY = static_cast<int>(desc.DesktopCoordinates.top);
        m_width = static_cast<int>(desc.DesktopCoordinates.right -
            desc.DesktopCoordinates.left);
        m_height = static_cast<int>(desc.DesktopCoordinates.bottom -
            desc.DesktopCoordinates.top);

        if (!createStagingTexture()) { return false; }

        m_useDxgi = true;
        return true;
    }

    bool createStagingTexture()
    {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = static_cast<UINT>(m_width);
        desc.Height = static_cast<UINT>(m_height);
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

        HRESULT hr = m_d3dDevice->CreateTexture2D(&desc, nullptr, &m_stagingTex);
        return SUCCEEDED(hr);
    }

    /// Attempt to acquire the next desktop frame via DXGI.
    /// Returns a null QImage if no new frame was available within the timeout.
    QImage captureDxgi()
    {
        DXGI_OUTDUPL_FRAME_INFO frameInfo{};
        ComPtr<IDXGIResource>   resource;

        // Block up to one full frame interval waiting for the next frame.
        // With timeout=0 the call returns immediately when the app is
        // backgrounded/minimised (DWM composes less frequently), causing
        // near-zero FPS on the viewer side.  Using the frame interval here
        // lets the OS deliver the frame as soon as DWM presents it.
        const UINT timeoutMs = static_cast<UINT>(1000 / qBound(1, targetFps, 120));
        HRESULT hr = m_duplication->AcquireNextFrame(timeoutMs, &frameInfo, &resource);

        if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
            return {};
        }

        if (hr == DXGI_ERROR_ACCESS_LOST) {
            // Output changed (resolution / orientation) – reinitialise.
            releaseDxgi();
            if (!initDxgi()) {
                m_useDxgi = false;
                emit captureError(QStringLiteral(
                    "DXGI access lost and re-init failed; switched to GDI fallback"));
            }
            return {};
        }

        if (FAILED(hr)) {
            emit captureError(
                QStringLiteral("IDXGIOutputDuplication::AcquireNextFrame failed: 0x")
                + QString::number(static_cast<unsigned long>(hr), 16));
            m_useDxgi = false;
            return {};
        }

        QImage result;

        ComPtr<ID3D11Texture2D> desktopTex;
        hr = resource.As(&desktopTex);
        if (SUCCEEDED(hr)) {
            m_d3dContext->CopyResource(m_stagingTex.Get(), desktopTex.Get());

            D3D11_MAPPED_SUBRESOURCE mapped{};
            hr = m_d3dContext->Map(m_stagingTex.Get(), 0,
                D3D11_MAP_READ, 0, &mapped);
            if (SUCCEEDED(hr)) {
                // Build a QImage that owns a deep copy of the pixel data.
                result = QImage(m_width, m_height, QImage::Format_ARGB32);
                for (int row = 0; row < m_height; ++row) {
                    const auto* src = reinterpret_cast<const uchar*>(mapped.pData)
                        + static_cast<std::size_t>(row) * mapped.RowPitch;
                    uchar* dst = result.scanLine(row);
                    std::memcpy(dst, src,
                        static_cast<std::size_t>(m_width) * 4u);
                }
                m_d3dContext->Unmap(m_stagingTex.Get(), 0);
            }
        }

        m_duplication->ReleaseFrame();
        return result;
    }

    void releaseDxgi()
    {
        m_duplication.Reset();
        m_stagingTex.Reset();
        m_d3dContext.Reset();
        m_d3dDevice.Reset();
    }

    // -----------------------------------------------------------------------
    // Members
    // -----------------------------------------------------------------------

    bool m_useDxgi{ false };
    int  m_originX{ 0 };   ///< Monitor left edge in virtual-desktop coords
    int  m_originY{ 0 };   ///< Monitor top  edge in virtual-desktop coords
    int  m_width{ 0 };
    int  m_height{ 0 };

    ComPtr<ID3D11Device>           m_d3dDevice;
    ComPtr<ID3D11DeviceContext>    m_d3dContext;
    ComPtr<IDXGIOutputDuplication> m_duplication;
    ComPtr<ID3D11Texture2D>        m_stagingTex;
};

// ---------------------------------------------------------------------------
// MOC include – CaptureWorker is defined in this .cpp, so MOC output must be
// included here rather than compiled separately.
// ---------------------------------------------------------------------------
#include "ScreenCapturer.moc"

// ---------------------------------------------------------------------------
// ScreenCapturer implementation
// ---------------------------------------------------------------------------

ScreenCapturer::ScreenCapturer(QObject* parent)
    : QObject(parent)
{
}

ScreenCapturer::~ScreenCapturer()
{
    stop();
}

bool ScreenCapturer::start(int monitorIndex, int targetFps)
{
    // CAS 0 → 1; if it was already 1, another session is active.
    if (!m_running.testAndSetAcquire(0, 1)) {
        return false;
    }

    m_thread = new QThread(this);
    m_worker = new CaptureWorker();          // no parent – will be moved to thread
    m_worker->monitorIndex = monitorIndex;
    m_worker->targetFps = targetFps;
    m_worker->runningFlag = &m_running;

    m_worker->moveToThread(m_thread);

    // Forward worker signals to our own signals (queued across threads).
    QObject::connect(m_worker, &CaptureWorker::frameReady,
        this, &ScreenCapturer::frameReady);
    QObject::connect(m_worker, &CaptureWorker::captureRegionReady,
        this, &ScreenCapturer::captureRegionReady);
    QObject::connect(m_worker, &CaptureWorker::captureError,
        this, [this](const QString& msg) {
            m_running.storeRelease(0);
            emit captureError(msg);
        });

    // Start the capture loop when the thread starts.
    QObject::connect(m_thread, &QThread::started,
        m_worker, &CaptureWorker::run);

    // Clean up worker and thread objects when the thread finishes.
    QObject::connect(m_thread, &QThread::finished,
        m_worker, &QObject::deleteLater);
    QObject::connect(m_thread, &QThread::finished,
        m_thread, &QObject::deleteLater);

    m_thread->start();
    return true;
}

void ScreenCapturer::stop()
{
    // Signal the loop to exit.
    m_running.storeRelease(0);

    if (m_thread) {
        m_thread->quit();
        if (!m_thread->wait(500)) {
            m_thread->terminate();
            m_thread->wait(200);
        }
        // m_thread and m_worker are deleted via deleteLater connections above.
        m_thread = nullptr;
        m_worker = nullptr;
    }
}

void ScreenCapturer::setQuality(int quality)
{
    m_jpegQuality = qBound(60, quality, 90);
}

bool ScreenCapturer::isRunning() const
{
    return m_running.loadAcquire() == 1;
}
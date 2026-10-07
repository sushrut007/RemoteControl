#include "ScreenCapturer.h"
#include "ScreenCapturerDxgi.h"

#include <QGuiApplication>
#include <QPixmap>
#include <QScreen>
#include <QTimer>
#include <QtGlobal>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace darpan::platform {

ScreenCapturer::ScreenCapturer(QObject *parent)
    : QObject(parent)
    , m_timer(new QTimer(this))
{
    connect(m_timer, &QTimer::timeout, this, [this]() {
        QSize streamSize;
        const QImage frame = capturePrimaryMonitor(&streamSize);
        if (!frame.isNull()) {
            emit frameCaptured(frame, streamSize);
        }
    });
}

ScreenCapturer::~ScreenCapturer()
{
    stop();
}

void ScreenCapturer::start(int intervalMs)
{
    m_timer->setInterval(intervalMs);
    if (!m_timer->isActive()) {
        m_timer->start();
    }
    m_active = true;

    QSize streamSize;
    const QImage frame = capturePrimaryMonitor(&streamSize);
    if (!frame.isNull()) {
        emit frameCaptured(frame, streamSize);
    }
}

void ScreenCapturer::stop()
{
    m_timer->stop();
    m_active = false;
}

#ifdef _WIN32
static QImage captureWithGdiBitBlt()
{
    const int width = GetSystemMetrics(SM_CXSCREEN);
    const int height = GetSystemMetrics(SM_CYSCREEN);
    if (width <= 0 || height <= 0) {
        return {};
    }

    HDC screenDc = GetDC(nullptr);
    if (!screenDc) {
        return {};
    }
    HDC memDc = CreateCompatibleDC(screenDc);
    HBITMAP bitmap = CreateCompatibleBitmap(screenDc, width, height);
    HGDIOBJ old = SelectObject(memDc, bitmap);
    BitBlt(memDc, 0, 0, width, height, screenDc, 0, 0, SRCCOPY | CAPTUREBLT);

    BITMAPINFOHEADER bi{};
    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = width;
    bi.biHeight = -height;
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    bi.biCompression = BI_RGB;

    QImage image(width, height, QImage::Format_ARGB32);
    GetDIBits(memDc, bitmap, 0, UINT(height), image.bits(), reinterpret_cast<BITMAPINFO *>(&bi),
              DIB_RGB_COLORS);

    SelectObject(memDc, old);
    DeleteObject(bitmap);
    DeleteDC(memDc);
    ReleaseDC(nullptr, screenDc);
    return image;
}
#endif

QImage ScreenCapturer::capturePrimaryMonitor(QSize *streamSizeOut)
{
    QImage image;

    // Default: Qt screen grab (no D3D). Avoids per-frame DXGI init under the debugger.
    if (QScreen *screen = QGuiApplication::primaryScreen()) {
        image = screen->grabWindow(0).toImage();
        if (!image.isNull()) {
            m_backend = QStringLiteral("qt");
        }
    }

#ifdef _WIN32
    if (image.isNull()) {
        image = captureWithGdiBitBlt();
        if (!image.isNull()) {
            m_backend = QStringLiteral("gdi");
        }
    }
#endif

    if (image.isNull() && qEnvironmentVariableIsSet("DARPAN_CAPTURE_DXGI") && !m_dxgiRejected) {
        QString dxgiError;
        if (capturePrimaryMonitorDxgi(&image, &dxgiError)) {
            m_backend = QStringLiteral("dxgi");
        } else {
            m_dxgiRejected = true;
        }
    }

    if (image.isNull()) {
        return {};
    }

    const QSize streamSize = image.size();
    if (image.width() > 1280) {
        image = image.scaled(1280, image.height() * 1280 / image.width(), Qt::IgnoreAspectRatio,
                             Qt::SmoothTransformation);
    }
    if (streamSizeOut) {
        *streamSizeOut = streamSize;
    }
    return image;
}

} // namespace darpan::platform

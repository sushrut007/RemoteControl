#include "ScreenCapturerDxgi.h"

#include <QImage>
#include <QString>
#include <QRgb>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#endif

namespace darpan::platform {

bool capturePrimaryMonitorDxgi(QImage *outImage, QString *errorOut)
{
#ifdef _WIN32
    using Microsoft::WRL::ComPtr;
    if (!outImage) {
        return false;
    }

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    D3D_FEATURE_LEVEL level{};
    const HRESULT hrDevice = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
                                               D3D11_SDK_VERSION, &device, &level, &context);
    if (FAILED(hrDevice)) {
        if (errorOut) {
            *errorOut = QStringLiteral("D3D11CreateDevice failed");
        }
        return false;
    }

    ComPtr<IDXGIDevice> dxgiDevice;
    if (FAILED(device.As(&dxgiDevice))) {
        return false;
    }
    ComPtr<IDXGIAdapter> adapter;
    dxgiDevice->GetAdapter(&adapter);
    ComPtr<IDXGIOutput> output;
    if (FAILED(adapter->EnumOutputs(0, &output))) {
        return false;
    }
    ComPtr<IDXGIOutput1> output1;
    if (FAILED(output.As(&output1))) {
        return false;
    }

    ComPtr<IDXGIOutputDuplication> duplication;
    if (FAILED(output1->DuplicateOutput(device.Get(), &duplication))) {
        if (errorOut) {
            *errorOut = QStringLiteral("DuplicateOutput failed (try GDI fallback)");
        }
        return false;
    }

    ComPtr<IDXGIResource> resource;
    DXGI_OUTDUPL_FRAME_INFO frameInfo{};
    const HRESULT hrAcquire = duplication->AcquireNextFrame(100, &frameInfo, &resource);
    if (FAILED(hrAcquire)) {
        if (errorOut) {
            *errorOut = QStringLiteral("AcquireNextFrame timeout");
        }
        return false;
    }

    ComPtr<ID3D11Texture2D> texture;
    resource.As(&texture);
    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);

    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(device->CreateTexture2D(&desc, nullptr, &staging))) {
        duplication->ReleaseFrame();
        return false;
    }
    context->CopyResource(staging.Get(), texture.Get());

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
        duplication->ReleaseFrame();
        return false;
    }

    QImage image(int(desc.Width), int(desc.Height), QImage::Format_ARGB32);
    const auto *src = static_cast<const uint8_t *>(mapped.pData);
    for (UINT h = 0; h < desc.Height; ++h) {
        auto *dest = reinterpret_cast<QRgb *>(image.scanLine(int(h)));
        const auto *row = reinterpret_cast<const QRgb *>(src + h * mapped.RowPitch);
        for (UINT w = 0; w < desc.Width; ++w) {
            const QRgb px = row[w];
            dest[w] = qRgba(qBlue(px), qGreen(px), qRed(px), qAlpha(px));
        }
    }
    context->Unmap(staging.Get(), 0);
    duplication->ReleaseFrame();

    if (image.width() > 1280) {
        *outImage = image.scaled(1280, image.height() * 1280 / image.width(), Qt::IgnoreAspectRatio,
                                 Qt::SmoothTransformation);
    } else {
        *outImage = image;
    }
    return true;
#else
    Q_UNUSED(outImage);
    if (errorOut) {
        *errorOut = QStringLiteral("DXGI capture is Windows-only");
    }
    return false;
#endif
}

} // namespace darpan::platform

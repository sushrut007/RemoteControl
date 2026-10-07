#pragma once

#include <QImage>

namespace darpan::platform {

/// DXGI Desktop Duplication (Windows 8+). Returns false if unavailable.
bool capturePrimaryMonitorDxgi(QImage *outImage, QString *errorOut = nullptr);

} // namespace darpan::platform

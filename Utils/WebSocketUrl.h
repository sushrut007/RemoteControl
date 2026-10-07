#pragma once

#include <QString>

namespace darpan::util {

/// Ensures signaling URL points at the FastAPI `/ws` endpoint.
QString normalizeSignalingWebSocketUrl(const QString &url);

} // namespace darpan::util

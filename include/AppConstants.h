#pragma once

#include <QString>

namespace darpan {

inline QString defaultSignalingUrl()
{
    return QStringLiteral("ws://127.0.0.1:8765/ws");
}

inline QString defaultStunServer()
{
    return QStringLiteral("stun:stun.l.google.com:19302");
}

} // namespace darpan

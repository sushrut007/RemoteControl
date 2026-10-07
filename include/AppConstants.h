#pragma once

#include <QString>

namespace darpan {

inline QString defaultSignalingUrl()
{
    return QStringLiteral("wss://remotecontrol.sushrutmakes.qzz.io/ws");
}

inline QString defaultStunServer()
{
    return QStringLiteral("stun:stun.l.google.com:19302");
}

} // namespace darpan

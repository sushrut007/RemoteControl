#pragma once

#include <QString>
#include "Secrets.h"

namespace darpan {

inline QString defaultSignalingUrl()
{
    return secrets::defaultSignalingUrl();
}

inline QString defaultStunServer()
{
    return secrets::defaultStunServer();
}

} // namespace darpan

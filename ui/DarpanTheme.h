#pragma once

#include <QString>

namespace DarpanTheme {

inline QString color(const char* hex) { return QString::fromLatin1(hex); }

// Surfaces
inline const char* kBgLowest      = "#0B0E14";
inline const char* kBgLow         = "#191C22";
inline const char* kBgContainer   = "#1D2026";
inline const char* kBgRaised      = "#21262D";
inline const char* kBgHigh        = "#272A31";
inline const char* kBgHighest     = "#32353C";
inline const char* kBgModal       = "#161B22";
inline const char* kBorderSubtle  = "#30363D";

// Text
inline const char* kTextPrimary   = "#F0F6FC";
inline const char* kTextSecondary = "#8B949E";
inline const char* kTextMuted     = "#484F58";
inline const char* kPrimary       = "#C0C1FF";

// Roles
inline const char* kRoleHost       = "#F59E0B";
inline const char* kRoleViewer     = "#10B981";
inline const char* kRoleController = "#6366F1";

// Semantic
inline const char* kError         = "#FFB4AB";
inline const char* kErrorContainer = "#93000A";
inline const char* kOnErrorContainer = "#FFDAD6";
inline const char* kSuccess       = "#10B981";

inline const char* roleColor(const QString& appType)
{
    if (appType == QLatin1String("host"))       return kRoleHost;
    if (appType == QLatin1String("controller")) return kRoleController;
    return kRoleViewer;
}

} // namespace DarpanTheme

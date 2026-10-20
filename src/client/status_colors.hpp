#pragma once

#include <QColor>

// The configured foreground already reflects the user's chosen surface,
// including a transparent desklet over a desktop background.
inline bool statusOnLightSurface(const QColor& foreground) {
    return foreground.lightness()<128;
}
inline QColor statusGreen(bool lightSurface) {
    return QColor(lightSurface ? "#187a3d" : "#2ecc71");
}
inline QColor statusYellow(bool lightSurface) {
    return QColor(lightSurface ? "#8a6400" : "#f1c40f");
}
inline QColor statusRed(bool lightSurface) {
    return QColor(lightSurface ? "#b42318" : "#e74c3c");
}

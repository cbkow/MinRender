#include "ui/platform/accent_color.h"

#include <QGuiApplication>
#include <QPalette>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <dwmapi.h>
#endif

namespace MR {

QColor systemAccentColor()
{
#ifdef _WIN32
    DWORD color = 0;
    BOOL opaque = FALSE;
    if (SUCCEEDED(DwmGetColorizationColor(&color, &opaque)))
    {
        // DWM returns ARGB. The alpha byte reflects compositor blending
        // (typically ~0xC4), not the intended UI color — force opaque.
        const int r = (color >> 16) & 0xFF;
        const int g = (color >>  8) & 0xFF;
        const int b =  color        & 0xFF;
        return QColor(r, g, b, 0xFF);
    }
#endif
    // macOS / Linux / Windows fallback — QPalette::Highlight tracks the
    // desktop theme on X11 and produces a reasonable default on macOS
    // until Phase 8 replaces this with controlAccentColor.
    return QGuiApplication::palette().color(QPalette::Highlight);
}

} // namespace MR

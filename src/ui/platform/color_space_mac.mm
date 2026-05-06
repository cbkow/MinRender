#include "ui/platform/color_space.h"

#import <AppKit/AppKit.h>

#include <QWindow>

namespace MR {

void pinSRgbColorSpace(QWindow* window)
{
    if (!window)
        return;

    // QWindow::winId() on macOS returns the underlying NSView pointer.
    // Walk up to its NSWindow and set the colour space explicitly to
    // sRGB. The system colour-manages from this tagged surface to
    // whatever the display gamut is, so the cobalt accent looks the
    // same on a sRGB Dell monitor as on a P3 XDR panel.
    NSView* nsView = reinterpret_cast<NSView*>(window->winId());
    if (!nsView)
        return;

    NSWindow* nsWindow = [nsView window];
    if (!nsWindow)
        return;

    nsWindow.colorSpace = [NSColorSpace sRGBColorSpace];
}

} // namespace MR

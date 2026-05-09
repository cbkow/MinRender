#pragma once

class QWindow;

namespace MR {

// Tag the NSWindow's color space as sRGB. This is belt-and-suspenders
// alongside the QSurfaceFormat sRGB pin in main_qt.cpp — the surface
// format governs the CAMetalLayer that QQuickWindow actually renders
// into (which is what fixed the visible oversaturation), while this
// NSWindow tag governs how the system labels the window for things
// like screenshots and Window Server compositing.
//
// Windows / Linux: no-op. Windows D3D11 swap chains default to sRGB;
// Linux compositors don't colour-manage Qt surfaces.
void pinSRgbColorSpace(QWindow* window);

} // namespace MR

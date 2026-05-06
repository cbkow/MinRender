#pragma once

class QWindow;

namespace MR {

// Pin the window's surface to sRGB so our hex colour tokens render
// consistently across platforms. macOS displays default to Display P3
// (Liquid Retina XDR, modern external panels); without an explicit
// sRGB tag, the CAMetalLayer backing the QQuickWindow inherits the
// display's wide-gamut colour space and our sRGB values get
// oversaturated (#0189f1 reads more electric than intended).
//
// Windows / Linux: no-op. Windows defaults to sRGB; Linux compositors
// don't colour-manage Qt swap chains.
void pinSRgbColorSpace(QWindow* window);

} // namespace MR

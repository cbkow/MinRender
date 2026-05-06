#include "ui/platform/color_space.h"

// Non-macOS stub. CMake only compiles this file when NOT building for
// Apple — the macOS implementation lives in color_space_mac.mm and pins
// the NSWindow's colour space to sRGB. Windows defaults to sRGB so
// nothing to do; Linux compositors don't colour-manage Qt swap chains.
namespace MR {

void pinSRgbColorSpace(QWindow*)
{
}

} // namespace MR

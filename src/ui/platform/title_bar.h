#pragma once

class QWindow;

namespace MR {

// Request a dark-themed non-client area (title bar + borders) for the given
// window. Windows 10 20H1+ and Windows 11: applied via DwmSetWindowAttribute.
// macOS and Linux: no-op — Qt follows the system appearance automatically
// on macOS, and X11/Wayland title bars are theme-driven.
void enableDarkTitleBar(QWindow* window);

} // namespace MR

#include "ui/platform/title_bar.h"

#include <QWindow>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <dwmapi.h>
#endif

namespace MR {

#ifdef _WIN32

// DWMWA_USE_IMMERSIVE_DARK_MODE. Documented as 20 starting with Windows 10
// version 2004 (20H1); older Windows 10 builds (1809..1909) accept 19. Try
// the new value first, fall back to the old. Either call is a silent no-op
// on OS versions that don't recognize the attribute.
static constexpr DWORD kDwmUseImmersiveDarkMode     = 20;
static constexpr DWORD kDwmUseImmersiveDarkModeOld  = 19;

void enableDarkTitleBar(QWindow* window)
{
    if (!window)
        return;

    // Ensure the native window handle exists. For a QQuickWindow opened with
    // visible: true this is usually true by the time loadFromModule returns,
    // but create() is cheap and idempotent.
    window->create();

    HWND hwnd = reinterpret_cast<HWND>(window->winId());
    if (!hwnd)
        return;

    BOOL dark = TRUE;
    HRESULT hr = DwmSetWindowAttribute(
        hwnd, kDwmUseImmersiveDarkMode, &dark, sizeof(dark));
    if (FAILED(hr))
    {
        DwmSetWindowAttribute(
            hwnd, kDwmUseImmersiveDarkModeOld, &dark, sizeof(dark));
    }
}

#else

void enableDarkTitleBar(QWindow* /*window*/)
{
}

#endif

} // namespace MR

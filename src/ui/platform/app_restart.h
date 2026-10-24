#pragma once

#include <functional>
#include <string>

namespace MR {

// Microsoft Store auto-updates close a running minRender and do not start
// it again, which leaves a farm node offline until someone logs in. Two
// halves fix that (verified 2026-10-24 with a throwaway MSIX on the build
// box: an app that registered was relaunched from the new package version
// with its arguments ~0.3 s after the update closed it; one that did not
// register stayed closed):
//
// registerAppRestart(): RegisterApplicationRestart("--minimized"), so
// Windows relaunches the app after servicing closes it (and after a crash
// or hang once it has run 60 s). Only when running with package identity:
// a dev build should not respawn itself under a debugger. Returns the
// package full name, or an empty string when not packaged / not Windows.
//
// SessionEndWindow: servicing asks the app to close with
// WM_QUERYENDSESSION / WM_ENDSESSION (ENDSESSION_CLOSEAPP), sent to
// top-level windows only. A tray-only minRender may have none (the QML
// window's HWND goes away on close-to-tray), so this owns a hidden
// top-level window of its own and calls onEnd when the session ends.
//
// macOS / Linux: no-ops.
std::string registerAppRestart();

class SessionEndWindow
{
public:
    explicit SessionEndWindow(std::function<void()> onEnd);
    ~SessionEndWindow();

    SessionEndWindow(const SessionEndWindow&) = delete;
    SessionEndWindow& operator=(const SessionEndWindow&) = delete;

private:
    std::function<void()> m_onEnd;
    void* m_hwnd = nullptr;
};

} // namespace MR

#include "ui/platform/app_restart.h"

#include <utility>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <appmodel.h>
#endif

namespace MR {

#ifdef _WIN32

namespace {

const wchar_t* kWindowClass = L"MinRenderSessionEnd";

LRESULT CALLBACK sessionEndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_QUERYENDSESSION:
        return TRUE;
    case WM_ENDSESSION:
        // wParam FALSE: another app vetoed, the session goes on.
        if (wParam)
        {
            auto* self = reinterpret_cast<std::function<void()>*>(
                GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (self && *self)
                (*self)();
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

std::string registerAppRestart()
{
    UINT32 len = 0;
    if (GetCurrentPackageFullName(&len, nullptr) != ERROR_INSUFFICIENT_BUFFER)
        return {};  // APPMODEL_ERROR_NO_PACKAGE: loose / dev build
    std::wstring name(len, L'\0');
    if (GetCurrentPackageFullName(&len, name.data()) != ERROR_SUCCESS)
        return {};
    name.resize(len > 0 ? len - 1 : 0);  // len counts the terminator

    // Flags 0: restart after updates (no RESTART_NO_PATCH), crashes and
    // hangs. The command line is appended to the exe path.
    if (FAILED(RegisterApplicationRestart(L"--minimized", 0)))
        return {};

    std::string utf8;
    const int n = WideCharToMultiByte(CP_UTF8, 0, name.c_str(), (int)name.size(),
                                      nullptr, 0, nullptr, nullptr);
    utf8.resize(n);
    WideCharToMultiByte(CP_UTF8, 0, name.c_str(), (int)name.size(),
                        utf8.data(), n, nullptr, nullptr);
    return utf8;
}

SessionEndWindow::SessionEndWindow(std::function<void()> onEnd)
    : m_onEnd(std::move(onEnd))
{
    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = sessionEndProc;
    wc.hInstance = inst;
    wc.lpszClassName = kWindowClass;
    RegisterClassExW(&wc);  // fails harmlessly if already registered

    // Top-level but never shown. Not HWND_MESSAGE: message-only windows
    // do not receive the session-end broadcast.
    HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, kWindowClass, L"minRender",
                                WS_POPUP, 0, 0, 0, 0,
                                nullptr, nullptr, inst, nullptr);
    if (hwnd)
    {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&m_onEnd));
        m_hwnd = hwnd;
    }
}

SessionEndWindow::~SessionEndWindow()
{
    if (m_hwnd)
        DestroyWindow(static_cast<HWND>(m_hwnd));
}

#else

std::string registerAppRestart() { return {}; }

SessionEndWindow::SessionEndWindow(std::function<void()> onEnd)
    : m_onEnd(std::move(onEnd)) {}

SessionEndWindow::~SessionEndWindow() = default;

#endif

} // namespace MR

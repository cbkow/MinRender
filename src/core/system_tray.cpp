#include "core/system_tray.h"

#ifdef _WIN32

#include <cstring>
#include <filesystem>

namespace MR {

static const wchar_t* s_trayIconFile = L"resources\\icons\\minrender.ico";

SystemTray::~SystemTray()
{
    if (m_initialized)
        shutdown();
}

bool SystemTray::init()
{
    // Register window class
    WNDCLASSW wc = {};
    wc.lpfnWndProc = wndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"MinRenderTray";
    RegisterClassW(&wc);

    // Create message-only window
    m_hwnd = CreateWindowExW(
        0, L"MinRenderTray", L"MinRender Tray",
        0, 0, 0, 0, 0,
        HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);

    if (!m_hwnd)
        return false;

    SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    loadIcon();

    // Add tray icon
    m_nid.cbSize = sizeof(NOTIFYICONDATAW);
    m_nid.hWnd = m_hwnd;
    m_nid.uID = 1;
    m_nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    m_nid.uCallbackMessage = WM_TRAYICON;
    m_nid.hIcon = m_icon;
    wcscpy_s(m_nid.szTip, L"MinRender");

    Shell_NotifyIconW(NIM_ADD, &m_nid);

    m_initialized = true;
    return true;
}

void SystemTray::shutdown()
{
    if (!m_initialized)
        return;

    Shell_NotifyIconW(NIM_DELETE, &m_nid);

    if (m_icon)
    {
        DestroyIcon(m_icon);
        m_icon = nullptr;
    }

    if (m_hwnd)
    {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }

    UnregisterClassW(L"MinRenderTray", GetModuleHandleW(nullptr));
    m_initialized = false;
}

void SystemTray::setTooltip(const std::string& text)
{
    if (!m_initialized)
        return;

    // Convert UTF-8 to wide
    wchar_t wide[128] = {};
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wide, 127);
    wcscpy_s(m_nid.szTip, wide);
    m_nid.uFlags = NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &m_nid);
}

void SystemTray::setStatusText(const std::string& text)
{
    m_statusText = text;
}

void SystemTray::setNodeActive(bool active)
{
    m_nodeActive = active;
}

void SystemTray::loadIcon()
{
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::filesystem::path exeDir = std::filesystem::path(exePath).parent_path();
    auto icoPath = exeDir / s_trayIconFile;

    m_icon = static_cast<HICON>(LoadImageW(
        nullptr, icoPath.wstring().c_str(),
        IMAGE_ICON, 16, 16, LR_LOADFROMFILE));

    // Fallback: LoadIcon from the exe's embedded resource (same .ico, ID 1)
    if (!m_icon)
        m_icon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1));
}

void SystemTray::showContextMenu()
{
    HMENU menu = CreatePopupMenu();

    // Title (disabled)
    AppendMenuW(menu, MF_STRING | MF_DISABLED | MF_GRAYED, 0, L"MinRender");

    // Status line (disabled) — single source of agent status in the tray UI
    wchar_t statusWide[128] = {};
    std::string statusLine = "Status: " + m_statusText;
    MultiByteToWideChar(CP_UTF8, 0, statusLine.c_str(), -1, statusWide, 127);
    AppendMenuW(menu, MF_STRING | MF_DISABLED | MF_GRAYED, 0, statusWide);

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    AppendMenuW(menu, MF_STRING, IDM_SHOW, L"Show Window");

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    if (m_nodeActive)
        AppendMenuW(menu, MF_STRING, IDM_TOGGLE, L"Stop Node");
    else
        AppendMenuW(menu, MF_STRING, IDM_TOGGLE, L"Resume Node");

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    AppendMenuW(menu, MF_STRING, IDM_EXIT, L"Exit");

    // Required Win32 workaround: SetForegroundWindow before TrackPopupMenu
    SetForegroundWindow(m_hwnd);

    POINT pt;
    GetCursorPos(&pt);
    TrackPopupMenu(menu, TPM_RIGHTALIGN | TPM_BOTTOMALIGN, pt.x, pt.y, 0, m_hwnd, nullptr);

    // Required Win32 workaround: post dummy message after TrackPopupMenu
    PostMessageW(m_hwnd, WM_NULL, 0, 0);

    DestroyMenu(menu);
}

LRESULT CALLBACK SystemTray::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    auto* self = reinterpret_cast<SystemTray*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg)
    {
    case WM_TRAYICON:
        if (self)
        {
            switch (LOWORD(lParam))
            {
            case WM_LBUTTONDBLCLK:
                if (self->onShowWindow)
                    self->onShowWindow();
                break;
            case WM_RBUTTONUP:
                self->showContextMenu();
                break;
            }
        }
        return 0;

    case WM_SHOW_WINDOW:
        // Posted by SingleInstance from a second process
        if (self && self->onShowWindow)
            self->onShowWindow();
        return 0;

    case WM_COMMAND:
        if (self)
        {
            switch (LOWORD(wParam))
            {
            case IDM_SHOW:
                if (self->onShowWindow)
                    self->onShowWindow();
                break;
            case IDM_TOGGLE:
                if (self->onStopResume)
                    self->onStopResume();
                break;
            case IDM_EXIT:
                if (self->onExit)
                    self->onExit();
                break;
            }
        }
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace MR

#else

// Stub for non-Windows
namespace MR {

SystemTray::~SystemTray() {}
bool SystemTray::init() { return true; }
void SystemTray::shutdown() {}
void SystemTray::setTooltip(const std::string&) {}
void SystemTray::setStatusText(const std::string&) {}
void SystemTray::setNodeActive(bool) {}

} // namespace MR

#endif

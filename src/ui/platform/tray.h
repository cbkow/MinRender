#pragma once

#include <functional>
#include <memory>
#include <string>

class QAction;
class QMenu;
class QSystemTrayIcon;

namespace MR {

// Qt QSystemTrayIcon wrapper. Mirrors the callback/setter shape of
// MR::SystemTray (src/core/system_tray.h) so main_qt.cpp can wire it
// the same way main_headless.cpp wires the legacy Win32 tray.
class Tray
{
public:
    Tray();
    ~Tray();

    Tray(const Tray&) = delete;
    Tray& operator=(const Tray&) = delete;

    // Returns false if the host has no system tray (e.g. vanilla GNOME
    // without an extension). Caller should degrade to hide-on-close off.
    bool init();
    void shutdown();

    void setTooltip(const std::string& text);
    void setStatusText(const std::string& text);
    void setNodeActive(bool active);

    std::function<void()> onShowWindow;
    std::function<void()> onStopResume;
    std::function<void()> onExit;

private:
    void updateMenuLabels();

    std::unique_ptr<QSystemTrayIcon> m_icon;
    std::unique_ptr<QMenu> m_menu;
    QAction* m_showAction = nullptr;
    QAction* m_stopResumeAction = nullptr;
    QAction* m_exitAction = nullptr;
    std::string m_statusText;
    bool m_nodeActive = false;
    bool m_initialized = false;
};

} // namespace MR

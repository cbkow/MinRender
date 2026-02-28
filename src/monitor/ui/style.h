#pragma once
#include <imgui.h>

struct GLFWwindow;

namespace MR {

struct Fonts
{
    static inline ImFont* regular = nullptr;
    static inline ImFont* bold    = nullptr;
    static inline ImFont* italic  = nullptr;
    static inline ImFont* mono    = nullptr;
    static inline ImFont* icons   = nullptr;
};

void loadFonts();
void setupStyle();
void enableDarkTitleBar(GLFWwindow* window);

// Returns the Windows accent color (or fallback warm gray).
ImVec4 getAccentColor();

// Draw a custom panel header: icon + bold title + close (X) button.
// Returns true if close was clicked.
bool panelHeader(const char* title, const char* icon, bool& visible);

// Draw an X close button in the top-right corner of the current window.
// Returns true if clicked. Call right after BeginPopupModal.
bool modalCloseButton(const char* id);

// Material Symbols Sharp codepoints
namespace Icons {
    inline const char* Nodes    = reinterpret_cast<const char*>(u8"\uE1AE"); // devices
    inline const char* Jobs     = reinterpret_cast<const char*>(u8"\uE8F9"); // view_list
    inline const char* Detail   = reinterpret_cast<const char*>(u8"\uE85D"); // info
    inline const char* Log      = reinterpret_cast<const char*>(u8"\uE868"); // terminal / receipt_long
    inline const char* Settings = reinterpret_cast<const char*>(u8"\uE8B8"); // settings
    inline const char* Network  = reinterpret_cast<const char*>(u8"\uE894"); // language / public
    inline const char* Render   = reinterpret_cast<const char*>(u8"\uE40B"); // movie
    inline const char* Tags     = reinterpret_cast<const char*>(u8"\uE893"); // label
    inline const char* Agent    = reinterpret_cast<const char*>(u8"\uEA77"); // smart_toy
    inline const char* Cleanup  = reinterpret_cast<const char*>(u8"\uE872"); // delete_sweep
    inline const char* Info     = reinterpret_cast<const char*>(u8"\uE88E"); // info
    inline const char* Style    = reinterpret_cast<const char*>(u8"\uE40A"); // palette
    inline const char* Folder   = reinterpret_cast<const char*>(u8"\uE2C7"); // folder
    inline const char* Close    = reinterpret_cast<const char*>(u8"\uE5CD"); // close
    inline const char* Computer = reinterpret_cast<const char*>(u8"\uF346"); // computer
    inline const char* Wifi     = reinterpret_cast<const char*>(u8"\uF52A"); // wifi
    inline const char* WifiOff  = reinterpret_cast<const char*>(u8"\uF74B"); // wifi_off
    inline const char* Dual     = reinterpret_cast<const char*>(u8"\uE9E4"); // swap_horiz
}

} // namespace MR

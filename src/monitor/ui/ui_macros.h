#pragma once
#include <imgui.h>

namespace MR {

// Outline button: transparent bg + visible border, fill on hover
inline void PushOutlineButtonStyle() {
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  ImVec4(0.28f, 0.28f, 0.28f, 0.50f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,   ImVec4(0.15f, 0.15f, 0.15f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_Border,         ImVec4(0.45f, 0.45f, 0.45f, 0.50f));
}
inline void PopOutlineButtonStyle() { ImGui::PopStyleColor(4); }

// Outline header: transparent-ish bg + faint border, fill on hover
inline void PushOutlineHeaderStyle() {
    ImGui::PushStyleColor(ImGuiCol_Header,        ImVec4(0.25f, 0.25f, 0.25f, 0.15f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered,  ImVec4(0.28f, 0.28f, 0.28f, 0.50f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive,   ImVec4(0.15f, 0.15f, 0.15f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_Border,         ImVec4(0.25f, 0.25f, 0.25f, 0.50f));
}
inline void PopOutlineHeaderStyle() { ImGui::PopStyleColor(4); }

// CollapsingHeader with accent-colored icon overlay.
// Label needs leading spaces ("      Text") for icon room.
inline bool CollapsingHeaderWithIcon(const char* label, ImFont* icon_font,
                                      const char* icon, const ImVec4& color,
                                      ImGuiTreeNodeFlags flags = 0) {
    bool open = ImGui::CollapsingHeader(label, flags);
    if (icon_font) {
        ImVec2 hmin = ImGui::GetItemRectMin();
        float icon_x = hmin.x + ImGui::GetTreeNodeToLabelSpacing() + 5.0f;
        float pad_y = ImGui::GetStyle().FramePadding.y;
        ImGui::PushFont(icon_font);
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(icon_x, hmin.y + pad_y),
            ImGui::ColorConvertFloat4ToU32(color), icon);
        ImGui::PopFont();
    }
    return open;
}

} // namespace MR

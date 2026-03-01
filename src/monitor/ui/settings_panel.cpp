#include "monitor/ui/settings_panel.h"
#include "monitor/monitor_app.h"
#include "monitor/rndr_supervisor.h"
#include "monitor/ui/style.h"
#include "monitor/ui/ui_macros.h"
#include "core/config.h"
#include "core/platform.h"

#include <imgui.h>
#include <nfd.h>
#include <cstring>

#ifdef _MSC_VER
#pragma warning(disable: 4996) // strncpy deprecation
#endif
#include <filesystem>
#include <algorithm>
#include <sstream>

namespace MR {

void SettingsPanel::init(MonitorApp* app)
{
    m_app = app;
    loadFromConfig();
}

void SettingsPanel::loadFromConfig()
{
    const auto& cfg = m_app->config();

    std::strncpy(m_syncRootBuf, cfg.sync_root.c_str(), sizeof(m_syncRootBuf) - 1);
    m_syncRootBuf[sizeof(m_syncRootBuf) - 1] = '\0';

    // Join tags with comma
    std::string tagsStr;
    for (size_t i = 0; i < cfg.tags.size(); i++)
    {
        if (i > 0) tagsStr += ", ";
        tagsStr += cfg.tags[i];
    }
    std::strncpy(m_tagsBuf, tagsStr.c_str(), sizeof(m_tagsBuf) - 1);
    m_tagsBuf[sizeof(m_tagsBuf) - 1] = '\0';

    m_httpPort = static_cast<int>(cfg.http_port);
    std::strncpy(m_ipOverrideBuf, cfg.ip_override.c_str(), sizeof(m_ipOverrideBuf) - 1);
    m_ipOverrideBuf[sizeof(m_ipOverrideBuf) - 1] = '\0';

    m_udpEnabled = cfg.udp_enabled;
    m_udpPort = static_cast<int>(cfg.udp_port);
    m_showNotifications = cfg.show_notifications;
    m_stagingEnabled = cfg.staging_enabled;
    m_rndrDualMode = cfg.rndr_dual_mode;
    m_fontScale = cfg.font_scale;
    m_savedSyncRoot = cfg.sync_root;
}

void SettingsPanel::applyToConfig()
{
    auto& cfg = m_app->config();

    cfg.sync_root = m_syncRootBuf;

    // Parse tags from comma-separated string
    cfg.tags.clear();
    std::istringstream ss(m_tagsBuf);
    std::string tag;
    while (std::getline(ss, tag, ','))
    {
        auto start = tag.find_first_not_of(" \t");
        auto end = tag.find_last_not_of(" \t");
        if (start != std::string::npos)
            cfg.tags.push_back(tag.substr(start, end - start + 1));
    }

    cfg.http_port = static_cast<uint16_t>(m_httpPort);
    cfg.ip_override = m_ipOverrideBuf;
    cfg.udp_enabled = m_udpEnabled;
    cfg.udp_port = static_cast<uint16_t>(m_udpPort);
    cfg.show_notifications = m_showNotifications;
    cfg.staging_enabled = m_stagingEnabled;
    cfg.rndr_dual_mode = m_rndrDualMode;
    cfg.font_scale = m_fontScale;

    ImGui::GetIO().FontGlobalScale = m_fontScale;
}

void SettingsPanel::drawFontSizeSection()
{
    ImGui::TextUnformatted("Font Size");
    ImGui::Spacing();

    ImGui::Text("Presets:");
    ImGui::SameLine();

    PushOutlineButtonStyle();
    if (ImGui::Button("Small"))
        m_fontScale = FONT_SCALE_SMALL;
    ImGui::SameLine();
    if (ImGui::Button("Medium"))
        m_fontScale = FONT_SCALE_MEDIUM;
    ImGui::SameLine();
    if (ImGui::Button("Large"))
        m_fontScale = FONT_SCALE_LARGE;
    ImGui::SameLine();
    if (ImGui::Button("X-Large"))
        m_fontScale = FONT_SCALE_XLARGE;
    PopOutlineButtonStyle();

    ImGui::Spacing();

    ImGui::Text("Custom Scale:");
    ImGui::SetNextItemWidth(-1);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.09f, 0.09f, 0.09f, 1.0f));
    ImGui::SliderFloat("##fontscale", &m_fontScale, 0.5f, 2.0f, "%.2fx");
    ImGui::PopStyleColor();
}

void SettingsPanel::drawFontPreview()
{
    ImGui::TextUnformatted("Font Preview");
    ImGui::Spacing();

    float originalScale = ImGui::GetIO().FontGlobalScale;
    float heightScale = 1.0f + (m_fontScale - 1.0f) * 0.65f;
    ImGui::BeginChild("FontPreview", ImVec2(-1, 120 * heightScale), ImGuiChildFlags_Borders);

    ImGui::GetIO().FontGlobalScale = m_fontScale;

    if (Fonts::regular)
    {
        ImGui::PushFont(Fonts::regular);
        ImGui::Text("Regular: The quick brown fox jumps over the lazy dog");
        ImGui::PopFont();
    }
    else
    {
        ImGui::Text("Regular: The quick brown fox jumps over the lazy dog");
    }

    ImGui::Spacing();

    if (Fonts::mono)
    {
        ImGui::PushFont(Fonts::mono);
        ImGui::Text("Mono: function main() { return 0; }");
        ImGui::PopFont();
    }
    else
    {
        ImGui::Text("Mono: function main() { return 0; }");
    }

    ImGui::Spacing();
    ImGui::TextDisabled("Scale: %.2fx", m_fontScale);

    ImGui::GetIO().FontGlobalScale = originalScale;
    ImGui::EndChild();
}

void SettingsPanel::render()
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImVec2 modalSize(viewport->WorkSize.x * 0.9f, viewport->WorkSize.y * 0.9f);
    ImGui::SetNextWindowSize(modalSize, ImGuiCond_Always);
    ImVec2 center(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f,
                  viewport->WorkPos.y + viewport->WorkSize.y * 0.5f);
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));

    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.09f, 0.09f, 0.09f, 1.0f));
    if (!ImGui::BeginPopupModal("Settings", nullptr,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove))
    {
        ImGui::PopStyleColor();
        return;
    }
    ImGui::PopStyleColor();

    if (modalCloseButton("SettingsClose"))
    {
        loadFromConfig();
        m_needsReload = true;
        ImGui::CloseCurrentPopup();
    }

    if (m_needsReload)
    {
        loadFromConfig();
        m_needsReload = false;
    }

    float buttonRowHeight = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
    ImGui::BeginChild("SettingsContent", ImVec2(0, -buttonRowHeight), ImGuiChildFlags_None);

    // --- Node Info ---
    PushOutlineHeaderStyle();
    if (CollapsingHeaderWithIcon("      Node Info", Fonts::icons, Icons::Info, getAccentColor(), ImGuiTreeNodeFlags_DefaultOpen))
    {
        const auto& id = m_app->identity();
        ImGui::Text("Node ID:  %s", id.nodeId().c_str());
        ImGui::Text("Hostname: %s", id.systemInfo().hostname.c_str());
        ImGui::Text("CPU:      %d cores", id.systemInfo().cpuCores);
        ImGui::Text("RAM:      %llu MB", static_cast<unsigned long long>(id.systemInfo().ramMB));
        ImGui::Text("GPU:      %s", id.systemInfo().gpuName.c_str());
        ImGui::Separator();
    }
    PopOutlineHeaderStyle();

    // --- Appearance ---
    PushOutlineHeaderStyle();
    if (CollapsingHeaderWithIcon("      Appearance", Fonts::icons, Icons::Style, getAccentColor(), ImGuiTreeNodeFlags_DefaultOpen))
    {
        drawFontSizeSection();
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        drawFontPreview();
        ImGui::Separator();
    }
    PopOutlineHeaderStyle();

    // --- Sync Root ---
    PushOutlineHeaderStyle();
    if (CollapsingHeaderWithIcon("      Sync Root", Fonts::icons, Icons::Folder, getAccentColor(), ImGuiTreeNodeFlags_DefaultOpen))
    {
        float browseWidth = ImGui::CalcTextSize("Browse...").x + ImGui::GetStyle().FramePadding.x * 2;
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - browseWidth - ImGui::GetStyle().ItemSpacing.x);
        ImGui::InputText("##syncroot", m_syncRootBuf, sizeof(m_syncRootBuf));
        ImGui::SameLine();
        PushOutlineButtonStyle();
        if (ImGui::Button("Browse..."))
        {
            nfdchar_t* outPath = nullptr;
            nfdresult_t result = NFD_PickFolder(&outPath, m_syncRootBuf[0] != '\0' ? m_syncRootBuf : nullptr);
            if (result == NFD_OKAY && outPath)
            {
                std::strncpy(m_syncRootBuf, outPath, sizeof(m_syncRootBuf) - 1);
                m_syncRootBuf[sizeof(m_syncRootBuf) - 1] = '\0';
                NFD_FreePath(outPath);
            }
        }
        PopOutlineButtonStyle();

        if (m_syncRootBuf[0] != '\0')
        {
            if (std::filesystem::is_directory(m_syncRootBuf))
                ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1.0f), "Directory exists");
            else
                ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.0f), "Directory not found");
        }
        ImGui::Separator();
    }
    PopOutlineHeaderStyle();

    // --- Network ---
    PushOutlineHeaderStyle();
    if (CollapsingHeaderWithIcon("      Network", Fonts::icons, Icons::Network, getAccentColor(), ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::SetNextItemWidth(120);
        ImGui::InputInt("HTTP Port", &m_httpPort, 0);
        if (m_httpPort < 1024) m_httpPort = 1024;
        if (m_httpPort > 65535) m_httpPort = 65535;

        ImGui::Spacing();
        ImGui::SetNextItemWidth(200);
        ImGui::InputText("IP Override", m_ipOverrideBuf, sizeof(m_ipOverrideBuf));
        ImGui::TextDisabled("Leave empty for auto-detection.");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Checkbox("Enable UDP Multicast", &m_udpEnabled);
        ImGui::TextDisabled("Fast peer discovery via LAN multicast. Disable for VPN/cloud networks.");

        if (m_udpEnabled)
        {
            ImGui::SetNextItemWidth(120);
            ImGui::InputInt("UDP Port", &m_udpPort, 0);
            if (m_udpPort < 1024) m_udpPort = 1024;
            if (m_udpPort > 65535) m_udpPort = 65535;
        }

#ifdef _WIN32
        ImGui::Spacing();
        PushOutlineButtonStyle();
        if (ImGui::Button("Add Firewall Rule"))
            addFirewallRule("MinRender", static_cast<uint16_t>(m_httpPort),
                            m_udpEnabled ? static_cast<uint16_t>(m_udpPort) : uint16_t(0));
        PopOutlineButtonStyle();
        ImGui::SameLine();
        if (m_udpEnabled)
            ImGui::TextDisabled("Allows TCP %d + UDP %d through Windows Firewall.", m_httpPort, m_udpPort);
        else
            ImGui::TextDisabled("Allows TCP %d through Windows Firewall.", m_httpPort);
#endif
        ImGui::Separator();
    }
    PopOutlineHeaderStyle();

    // --- Rendering ---
    PushOutlineHeaderStyle();
    if (CollapsingHeaderWithIcon("      Rendering", Fonts::icons, Icons::Render, getAccentColor(), ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Checkbox("Local Staging Area", &m_stagingEnabled);
        ImGui::TextDisabled("Render to a local temp folder, then copy to the network output path.");
        ImGui::TextDisabled("Prevents file corruption from cloud sync (Synology Drive, Dropbox, etc.).");
        ImGui::Separator();
    }
    PopOutlineHeaderStyle();

    // --- RNDR Dual Mode ---
    if (m_app->rndrSupervisor().isBinaryAvailable())
    {
        PushOutlineHeaderStyle();
        if (CollapsingHeaderWithIcon("      RNDR Dual Mode", Fonts::icons, Icons::Dual, getAccentColor(), ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Checkbox("Enable RNDR Dual Mode", &m_rndrDualMode);
            ImGui::TextDisabled("Run RNDR client during idle time. Automatically paused when rendering.");

            ImGui::Spacing();
            auto& rndr = m_app->rndrSupervisor();
            std::string status = rndr.statusText();

            ImVec4 statusColor;
            switch (rndr.state())
            {
            case RndrState::Running:
                statusColor = ImVec4(0.3f, 0.9f, 0.3f, 1.0f);
                break;
            case RndrState::Cooldown:
                statusColor = ImVec4(0.9f, 0.9f, 0.3f, 1.0f);
                break;
            case RndrState::RenderActive:
                statusColor = ImVec4(0.9f, 0.6f, 0.3f, 1.0f);
                break;
            default:
                statusColor = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
                break;
            }

            ImGui::TextColored(statusColor, "%s", status.c_str());
            ImGui::Separator();
        }
        PopOutlineHeaderStyle();
    }

    // --- Tags ---
    PushOutlineHeaderStyle();
    if (CollapsingHeaderWithIcon("      Node Tags", Fonts::icons, Icons::Tags, getAccentColor()))
    {
        ImGui::InputText("Tags (comma-separated)", m_tagsBuf, sizeof(m_tagsBuf));
        ImGui::Separator();
    }
    PopOutlineHeaderStyle();

    // --- Agent ---
    PushOutlineHeaderStyle();
    if (CollapsingHeaderWithIcon("      Agent", Fonts::icons, Icons::Agent, getAccentColor(), ImGuiTreeNodeFlags_DefaultOpen))
    {
        auto& supervisor = m_app->agentSupervisor();
        bool connected = supervisor.isAgentConnected();

        if (connected)
        {
            ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1.0f), "Rendering");
            ImGui::SameLine();
            ImGui::TextDisabled("(PID %u, %s)", supervisor.agentPid(),
                supervisor.agentState().empty() ? "unknown" : supervisor.agentState().c_str());
        }
        else
        {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Idle");
        }

        ImGui::TextDisabled("Agent is spawned automatically for each render chunk.");
        ImGui::Separator();
    }
    PopOutlineHeaderStyle();

    // --- Notifications ---
    ImGui::Checkbox("Show notifications", &m_showNotifications);

    ImGui::EndChild(); // SettingsContent

    // --- Save / Cancel ---
    ImGui::Separator();
    PushOutlineButtonStyle();
    if (ImGui::Button("Save", ImVec2(120, 0)))
    {
        std::string oldSyncRoot = m_savedSyncRoot;
        auto& cfg = m_app->config();
        uint16_t oldPort = cfg.http_port;
        std::string oldIpOverride = cfg.ip_override;
        bool oldUdpEnabled = cfg.udp_enabled;
        uint16_t oldUdpPort = cfg.udp_port;

        applyToConfig();
        m_app->saveConfig();

        // Staging takes effect on the next chunk — no farm restart needed
        m_app->renderCoordinator().setStagingEnabled(cfg.staging_enabled);

        bool needsRestart = (cfg.sync_root != oldSyncRoot) ||
                            (cfg.http_port != oldPort) ||
                            (cfg.ip_override != oldIpOverride) ||
                            (cfg.udp_enabled != oldUdpEnabled) ||
                            (cfg.udp_port != oldUdpPort);

        if (needsRestart)
        {
            m_app->stopFarm();
            if (!cfg.sync_root.empty() && std::filesystem::is_directory(cfg.sync_root))
                m_app->startFarm();
        }

        m_needsReload = true;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0)))
    {
        loadFromConfig();
        m_needsReload = true;
        ImGui::CloseCurrentPopup();
    }
    PopOutlineButtonStyle();

    ImGui::EndPopup();
}

} // namespace MR

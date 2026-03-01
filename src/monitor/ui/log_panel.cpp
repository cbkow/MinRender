#include "monitor/ui/log_panel.h"
#include "monitor/ui/style.h"
#include "monitor/ui/ui_macros.h"
#include "monitor/monitor_app.h"
#include "core/monitor_log.h"

#include <imgui.h>
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>

namespace MR {

void LogPanel::init(MonitorApp* app)
{
    m_app = app;
}

void LogPanel::render()
{
    if (!visible) return;

    if (ImGui::Begin("Log", nullptr, ImGuiWindowFlags_NoTitleBar))
    {
        panelHeader("Log", Icons::Log, visible);

        // Mode dropdown
        {
            std::string selectedJobId = m_app ? m_app->selectedJobId() : "";

            // Build current label
            std::string comboLabel;
            if (m_mode == Mode::MonitorLog)
                comboLabel = "Monitor Log";
            else if (m_mode == Mode::RemoteNodeLog)
                comboLabel = "Remote: " + (m_selectedRemoteHostname.empty()
                    ? m_selectedRemoteNodeId : m_selectedRemoteHostname);
            else if (!selectedJobId.empty())
                comboLabel = "Task Output: " + selectedJobId;
            else
                comboLabel = "Task Output";

            ImGui::SetNextItemWidth(300.0f);
            if (ImGui::BeginCombo("##LogMode", comboLabel.c_str()))
            {
                if (ImGui::Selectable("Monitor Log", m_mode == Mode::MonitorLog))
                    m_mode = Mode::MonitorLog;

                // Task Output option — always shown, label varies
                std::string taskLabel = selectedJobId.empty()
                    ? "Task Output"
                    : ("Task Output: " + selectedJobId);
                if (ImGui::Selectable(taskLabel.c_str(), m_mode == Mode::TaskOutput))
                    m_mode = Mode::TaskOutput;

                // Node Logs — local + alive peers
                if (m_app && m_app->isFarmRunning())
                {
                    // Local node
                    ImGui::Separator();
                    ImGui::TextDisabled("This Node");

                    const auto& id = m_app->identity();
                    std::string localHostname = id.systemInfo().hostname;
                    std::string localLabel = (localHostname.empty() ? id.nodeId() : localHostname) + " (local)";
                    bool localSelected = (m_mode == Mode::RemoteNodeLog &&
                                          m_selectedRemoteNodeId == id.nodeId());
                    if (ImGui::Selectable(("  " + localLabel).c_str(), localSelected))
                    {
                        m_mode = Mode::RemoteNodeLog;
                        m_selectedRemoteNodeId = id.nodeId();
                        m_selectedRemoteHostname = localHostname;
                        m_remoteLogCache.clear();
                        m_lastRemoteLogRefresh = {};
                    }

                    // Remote peers
                    ImGui::Separator();
                    ImGui::TextDisabled("Remote Node Logs");

                    auto peers = m_app->peerManager().getPeerSnapshot();
                    for (const auto& peer : peers)
                    {
                        if (!peer.is_alive) continue;
                        if (peer.node_id == id.nodeId()) continue; // skip local
                        std::string label = peer.hostname.empty() ? peer.node_id : peer.hostname;
                        bool isSelected = (m_mode == Mode::RemoteNodeLog &&
                                          m_selectedRemoteNodeId == peer.node_id);
                        if (ImGui::Selectable(("  " + label).c_str(), isSelected))
                        {
                            m_mode = Mode::RemoteNodeLog;
                            m_selectedRemoteNodeId = peer.node_id;
                            m_selectedRemoteHostname = peer.hostname;
                            m_remoteLogCache.clear();
                            m_lastRemoteLogRefresh = {};
                        }
                    }
                }

                ImGui::EndCombo();
            }
        }

        ImGui::SameLine();
        ImGui::Checkbox("Auto-scroll", &m_autoScroll);
        ImGui::Separator();

        switch (m_mode)
        {
            case Mode::MonitorLog:    renderMonitorLog(); break;
            case Mode::TaskOutput:    renderTaskOutput(); break;
            case Mode::RemoteNodeLog: renderRemoteNodeLog(); break;
        }
    }
    ImGui::End();
}

void LogPanel::renderMonitorLog()
{
    auto entries = MonitorLog::instance().getEntries();

    ImGui::BeginChild("LogScroll", ImVec2(0, 0), ImGuiChildFlags_None);

    if (Fonts::mono)
        ImGui::PushFont(Fonts::mono);

    for (const auto& entry : entries)
    {
        ImVec4 col(1, 1, 1, 1);
        if (entry.level == "WARN")
            col = ImVec4(1.0f, 0.85f, 0.0f, 1.0f);
        else if (entry.level == "ERROR")
            col = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
        else
            col = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);

        // Format timestamp: HH:MM:SS.mmm
        auto secs = entry.timestamp_ms / 1000;
        auto remainder = entry.timestamp_ms % 1000;
        time_t t = static_cast<time_t>(secs);
        struct tm tmBuf;
#ifdef _WIN32
        localtime_s(&tmBuf, &t);
#else
        localtime_r(&t, &tmBuf);
#endif
        char timeBuf[16];
        std::snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d.%03d",
                      tmBuf.tm_hour, tmBuf.tm_min, tmBuf.tm_sec, (int)remainder);

        ImGui::TextColored(col, "%s %s  [%s] %s",
            timeBuf,
            entry.level.c_str(),
            entry.category.c_str(),
            entry.message.c_str());
    }

    if (Fonts::mono)
        ImGui::PopFont();

    if (m_autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 10.0f)
        ImGui::SetScrollHereY(1.0f);

    ImGui::EndChild();
}

void LogPanel::renderTaskOutput()
{
    if (!m_app)
    {
        ImGui::TextDisabled("No app context");
        return;
    }

    std::string jobId = m_app->selectedJobId();
    if (jobId.empty())
    {
        ImGui::TextDisabled("No job selected");
        return;
    }

    // Refresh on cooldown
    auto now = std::chrono::steady_clock::now();
    bool needScan = (jobId != m_taskOutputJobId);
    if (!needScan)
    {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - m_lastTaskOutputScan).count();
        needScan = (elapsed >= 3000);
    }
    if (needScan)
        scanTaskOutput();

    if (m_chunkList.empty())
    {
        ImGui::TextDisabled("No task output available");
        return;
    }

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));

    // Left pane: chunk list (scale width with font size)
    float chunkListWidth = 200.0f * ImGui::GetIO().FontGlobalScale;
    ImGui::BeginChild("ChunkList", ImVec2(chunkListWidth, 0), ImGuiChildFlags_Borders);

    if (Fonts::mono)
        ImGui::PushFont(Fonts::mono);

    for (int i = 0; i < (int)m_chunkList.size(); ++i)
    {
        bool selected = (i == m_selectedChunkIndex);
        if (ImGui::Selectable(m_chunkList[i].displayLabel.c_str(), selected))
        {
            m_selectedChunkIndex = i;
            loadChunkContent(i);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s  %s", m_chunkList[i].nodeId.c_str(),
                              m_chunkList[i].displayLabel.c_str());
    }

    if (Fonts::mono)
        ImGui::PopFont();

    ImGui::EndChild();

    ImGui::SameLine();

    // Right pane: selected chunk content
    ImGui::BeginChild("ChunkContent", ImVec2(0, 0), ImGuiChildFlags_None);

    if (m_selectedChunkIndex < 0)
    {
        ImGui::TextDisabled("Select a chunk to view its output");
    }
    else
    {
        if (Fonts::mono)
            ImGui::PushFont(Fonts::mono);

        for (const auto& line : m_selectedChunkLines)
            ImGui::TextUnformatted(line.c_str());

        if (Fonts::mono)
            ImGui::PopFont();

        if (m_autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 10.0f)
            ImGui::SetScrollHereY(1.0f);
    }

    ImGui::EndChild();

    ImGui::PopStyleColor();
}

void LogPanel::scanTaskOutput()
{
    m_lastTaskOutputScan = std::chrono::steady_clock::now();

    if (!m_app) return;

    std::string jobId = m_app->selectedJobId();
    if (jobId.empty()) return;

    // Reset selection when job changes
    bool jobChanged = (jobId != m_taskOutputJobId);
    m_taskOutputJobId = jobId;
    if (jobChanged)
    {
        m_selectedChunkIndex = -1;
        m_selectedChunkLines.clear();
    }

    auto stdoutDir = m_app->farmPath() / "jobs" / jobId / "stdout";
    std::error_code ec;
    if (!std::filesystem::is_directory(stdoutDir, ec))
        return;

    std::vector<ChunkLogEntry> chunks;

    for (const auto& nodeEntry : std::filesystem::directory_iterator(stdoutDir, ec))
    {
        if (!nodeEntry.is_directory()) continue;
        std::string nodeId = nodeEntry.path().filename().string();

        for (const auto& fileEntry : std::filesystem::directory_iterator(nodeEntry.path(), ec))
        {
            if (!fileEntry.is_regular_file()) continue;
            std::string fname = fileEntry.path().filename().string();
            if (fname.size() < 5 || fname.substr(fname.size() - 4) != ".log")
                continue;

            // Parse: {rangeStr}_{timestamp_ms}.log
            std::string stem = fname.substr(0, fname.size() - 4);
            auto underPos = stem.rfind('_');
            if (underPos == std::string::npos) continue;

            ChunkLogEntry entry;
            entry.nodeId = nodeId;
            entry.rangeStr = stem.substr(0, underPos);
            try { entry.timestampMs = std::stoll(stem.substr(underPos + 1)); }
            catch (...) { continue; }
            entry.path = fileEntry.path();

            // Format timestamp as HH:MM:SS
            time_t t = static_cast<time_t>(entry.timestampMs / 1000);
            struct tm tmBuf;
#ifdef _WIN32
            localtime_s(&tmBuf, &t);
#else
            localtime_r(&t, &tmBuf);
#endif
            char timeBuf[16];
            std::strftime(timeBuf, sizeof(timeBuf), "%H:%M:%S", &tmBuf);

            entry.displayLabel = "f" + entry.rangeStr + "  " + timeBuf;
            chunks.push_back(std::move(entry));
        }
    }

    // Sort by rangeStr then timestamp
    std::sort(chunks.begin(), chunks.end(), [](const ChunkLogEntry& a, const ChunkLogEntry& b)
    {
        if (a.rangeStr != b.rangeStr) return a.rangeStr < b.rangeStr;
        return a.timestampMs < b.timestampMs;
    });

    // If selected chunk still exists, preserve selection; reload content if file changed
    if (m_selectedChunkIndex >= 0 && m_selectedChunkIndex < (int)m_chunkList.size())
    {
        const auto& oldPath = m_chunkList[m_selectedChunkIndex].path;
        int newIndex = -1;
        for (int i = 0; i < (int)chunks.size(); ++i)
        {
            if (chunks[i].path == oldPath)
            {
                newIndex = i;
                break;
            }
        }
        m_chunkList = std::move(chunks);
        m_selectedChunkIndex = newIndex;
        if (newIndex >= 0)
            loadChunkContent(newIndex); // reload to pick up appended content
    }
    else
    {
        m_chunkList = std::move(chunks);
    }
}

void LogPanel::loadChunkContent(int index)
{
    m_selectedChunkLines.clear();
    if (index < 0 || index >= (int)m_chunkList.size()) return;

    std::ifstream ifs(m_chunkList[index].path);
    if (!ifs.is_open()) return;

    std::string line;
    while (std::getline(ifs, line))
        m_selectedChunkLines.push_back(std::move(line));
}

void LogPanel::renderRemoteNodeLog()
{
    if (!m_app || m_selectedRemoteNodeId.empty())
    {
        ImGui::TextDisabled("No remote node selected");
        return;
    }

    if (!m_app->isFarmRunning())
    {
        ImGui::TextDisabled("Farm not running");
        return;
    }

    // Refresh every 3 seconds
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - m_lastRemoteLogRefresh).count();
    if (m_remoteLogCache.empty() || elapsed >= 3000)
    {
        m_remoteLogCache = MonitorLog::readNodeLog(
            m_app->farmPath(), m_selectedRemoteNodeId, 500);
        m_lastRemoteLogRefresh = now;
    }

    if (m_remoteLogCache.empty())
    {
        ImGui::TextDisabled("No log data available for this node");
        return;
    }

    ImGui::BeginChild("RemoteLogScroll", ImVec2(0, 0), ImGuiChildFlags_None);

    if (Fonts::mono)
        ImGui::PushFont(Fonts::mono);

    for (const auto& line : m_remoteLogCache)
    {
        ImVec4 col(0.7f, 0.7f, 0.7f, 1.0f);
        if (line.find("[WARN]") != std::string::npos)
            col = ImVec4(1.0f, 0.85f, 0.0f, 1.0f);
        else if (line.find("[ERROR]") != std::string::npos)
            col = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);

        ImGui::TextColored(col, "%s", line.c_str());
    }

    if (Fonts::mono)
        ImGui::PopFont();

    if (m_autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 10.0f)
        ImGui::SetScrollHereY(1.0f);

    ImGui::EndChild();
}

} // namespace MR

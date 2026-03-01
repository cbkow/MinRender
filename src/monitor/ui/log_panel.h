#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace MR {

class MonitorApp;

struct ChunkLogEntry
{
    std::string nodeId;
    std::string rangeStr;
    int64_t timestampMs = 0;
    std::filesystem::path path;
    std::string displayLabel; // "f000001-000010  nodeXYZ  12:34:56"
};

class LogPanel
{
public:
    void init(MonitorApp* app);
    void render();
    bool visible = true;

private:
    void renderMonitorLog();
    void renderTaskOutput();
    void scanTaskOutput();
    void loadChunkContent(int index);
    void renderRemoteNodeLog();

    MonitorApp* m_app = nullptr;
    bool m_autoScroll = true;

    // Mode
    enum class Mode { MonitorLog, TaskOutput, RemoteNodeLog };
    Mode m_mode = Mode::MonitorLog;

    // Task output state
    std::string m_taskOutputJobId;
    std::vector<ChunkLogEntry> m_chunkList;
    int m_selectedChunkIndex = -1;
    std::vector<std::string> m_selectedChunkLines;
    std::chrono::steady_clock::time_point m_lastTaskOutputScan;

    // Remote node log cache
    std::string m_selectedRemoteNodeId;
    std::string m_selectedRemoteHostname;
    std::vector<std::string> m_remoteLogCache;
    std::chrono::steady_clock::time_point m_lastRemoteLogRefresh;
};

} // namespace MR

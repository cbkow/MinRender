#pragma once

#include <chrono>
#include <filesystem>

namespace MR {

class MonitorApp;

class SubmissionWatcher
{
public:
    void init(MonitorApp* app, const std::filesystem::path& appDataDir);
    void poll();

private:
    void processSubmission(const std::filesystem::path& jsonPath);

    void pollDir(const std::filesystem::path& dir);

    MonitorApp* m_app = nullptr;
    std::filesystem::path m_submissionsDir;
    // Windows, transition from the pre-0.5.8 data folder: plug-ins not yet
    // redeployed still drop into %LOCALAPPDATA%\MinRender\submissions.
    // Processed files move to the NEW folder's processed\ either way.
    std::filesystem::path m_legacySubmissionsDir;
    std::chrono::steady_clock::time_point m_lastPoll;
};

} // namespace MR

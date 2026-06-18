#pragma once

#include <string>
#include <chrono>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

namespace MR {

enum class RndrState { Inactive, Running, RenderActive, Cooldown };

/// Manages the RNDR client lifecycle — launches during idle, kills during renders.
/// Detection and kill are by process name (TCPSVCS.EXE), not by PID,
/// because rndrclient.exe is a launcher that exits after spawning workers.
class RndrSupervisor
{
public:
    RndrSupervisor();
    ~RndrSupervisor();

    RndrSupervisor(const RndrSupervisor&) = delete;
    RndrSupervisor& operator=(const RndrSupervisor&) = delete;

    void detectBinary();
    void update(bool isRendering, bool dualModeEnabled);
    void shutdown();

    bool isBinaryAvailable() const { return m_binaryAvailable; }
    bool isRndrRunning() const;
    std::string statusText() const;
    RndrState state() const { return m_state; }

private:
    bool launchRndr();
    void killRndr();

    // Process-name-based checks (no handle tracking)
    bool isTcpsvcsRunning() const;
    bool isTcpsvcsResponsive() const;

    bool m_binaryAvailable = false;
    RndrState m_state = RndrState::Inactive;

#ifdef _WIN32
    std::wstring m_binaryPath;
#endif

    // Watchdog polling (every 5s)
    std::chrono::steady_clock::time_point m_lastWatchdogPoll;
    static constexpr int WATCHDOG_INTERVAL_SECONDS = 5;

    // Cooldown after render finishes (120s — let dispatch claim GPU first)
    std::chrono::steady_clock::time_point m_cooldownStart;
    static constexpr int COOLDOWN_SECONDS = 120;

    // Hang detection
    std::chrono::steady_clock::time_point m_hangDetectedTime;
    bool m_hangDetected = false;
    static constexpr int HANG_GRACE_SECONDS = 15;

    // Warmup (skip watchdog checks after launch)
    std::chrono::steady_clock::time_point m_launchTime;
    static constexpr int WARMUP_SECONDS = 15;

    // Startup delay — don't launch RNDR for 120s after app start,
    // giving MinRender's own dispatch a chance to claim the GPU first.
    std::chrono::steady_clock::time_point m_constructedTime;
    static constexpr int STARTUP_DELAY_SECONDS = 120;
};

} // namespace MR

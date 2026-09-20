#pragma once

#include <string>
#include <vector>
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
    void update(bool isRendering, bool dualModeEnabled,
                bool dccGateEnabled,
                const std::vector<std::string>& blockProcesses);
    void shutdown();

    bool isBinaryAvailable() const { return m_binaryAvailable; }
    bool isRndrRunning() const;
    std::string statusText() const;
    RndrState state() const { return m_state; }

private:
    bool launchRndr();
    void killRndr();

    // Returns the image name of a listed creative app running from
    // Program Files, or empty if none. Program Files is the whole
    // discriminator: writing there needs admin, and the dual-mode client
    // is a per-user install, so its own child processes can never be
    // mistaken for an artist's session however the vendor rearranges
    // their layout.
    std::string scanDccPresence(const std::vector<std::string>& names) const;
    void updateDccOccupancy(bool gateEnabled,
                            const std::vector<std::string>& names,
                            std::chrono::steady_clock::time_point now);

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

    // --- Creative-app gate (opt-in) ---------------------------------
    // Occupancy is sampled, never inferred from open/close events: an
    // app can vanish by crashing, by being killed, or because we were
    // restarted while it was running, and none of those produce a close
    // to observe. Stamping a timestamp whenever one is *seen* collapses
    // the hard block and the settle window into one comparison.
    std::chrono::steady_clock::time_point m_lastOccupiedSeen;
    std::chrono::steady_clock::time_point m_lastDccPoll;
    bool m_dccGateEnabled = false;
    bool m_dccPresent = false;
    std::string m_dccName;          // what we last saw, for status + logs
    static constexpr int DCC_POLL_SECONDS = 5;
    static constexpr int DCC_SETTLE_SECONDS = 600;   // 10 min
};

} // namespace MR

#include "monitor/rndr_supervisor.h"
#include "core/monitor_log.h"

#include <algorithm>
#include <filesystem>

#ifdef _WIN32
#include <TlHelp32.h>
#endif

namespace MR {

RndrSupervisor::RndrSupervisor()
    : m_lastWatchdogPoll(std::chrono::steady_clock::now())
    , m_cooldownStart()
    , m_hangDetectedTime()
    , m_launchTime()
    , m_constructedTime(std::chrono::steady_clock::now())
    // Assume recently occupied: we have no idea what was running before
    // we started, so serve a full settle before the first launch.
    , m_lastOccupiedSeen(std::chrono::steady_clock::now())
    , m_lastDccPoll()
{
}

RndrSupervisor::~RndrSupervisor()
{
    shutdown();
}

void RndrSupervisor::detectBinary()
{
#ifdef _WIN32
    wchar_t localAppData[MAX_PATH];
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH) == 0)
    {
        m_binaryAvailable = false;
        return;
    }

    std::filesystem::path path(localAppData);
    path /= L"MinRender";
    path /= L"rn";
    path /= L"rndrclient.exe";

    m_binaryAvailable = std::filesystem::exists(path);
    if (m_binaryAvailable)
    {
        m_binaryPath = path.wstring();
        MonitorLog::instance().info("rndr", "RNDR binary found: " + path.string());
    }
#else
    m_binaryAvailable = false;
#endif
}

void RndrSupervisor::update(bool isRendering, bool dualModeEnabled,
                            bool dccGateEnabled,
                            const std::vector<std::string>& blockProcesses)
{
#ifdef _WIN32
    if (!m_binaryAvailable)
        return;

    // Dual mode OFF -> kill and go inactive
    if (!dualModeEnabled)
    {
        if (m_state != RndrState::Inactive)
        {
            if (isTcpsvcsRunning())
                killRndr();
            m_state = RndrState::Inactive;
            m_hangDetected = false;
        }
        return;
    }

    auto now = std::chrono::steady_clock::now();

    updateDccOccupancy(dccGateEnabled, blockProcesses, now);

    // One comparison covers both tiers: a creative app running right now
    // makes this zero, and the same expression keeps holding for the
    // settle window after it disappears.
    const bool dccBlocked = m_dccGateEnabled
        && std::chrono::duration_cast<std::chrono::seconds>(
               now - m_lastOccupiedSeen).count() < DCC_SETTLE_SECONDS;

    switch (m_state)
    {
    case RndrState::Inactive:
        if (!isRendering)
        {
            // Don't launch RNDR during startup delay — let dispatch claim GPU first
            auto startupElapsed = std::chrono::duration_cast<std::chrono::seconds>(
                now - m_constructedTime).count();
            if (startupElapsed < STARTUP_DELAY_SECONDS)
                break;

            // Someone is working on this machine (or was, recently).
            if (dccBlocked)
                break;

            // Check if TCPSVCS is already running (user or previous session)
            if (isTcpsvcsRunning())
            {
                MonitorLog::instance().info("rndr", "TCPSVCS already running, adopting");
                m_launchTime = now;
                m_state = RndrState::Running;
            }
            else if (launchRndr())
            {
                m_state = RndrState::Running;
            }
        }
        else
        {
            m_state = RndrState::RenderActive;
        }
        break;

    case RndrState::Running:
        if (isRendering)
        {
            // Render started — kill RNDR immediately
            killRndr();
            m_state = RndrState::RenderActive;
            m_hangDetected = false;
            break;
        }

        // A creative app appeared — stand down immediately. Presence, not
        // the settle window, is the kill trigger: the settle only gates
        // relaunching.
        if (m_dccPresent)
        {
            killRndr();
            m_state = RndrState::Inactive;
            m_hangDetected = false;
            break;
        }

        // Watchdog poll every 5s
        {
            auto watchdogElapsed = std::chrono::duration_cast<std::chrono::seconds>(
                now - m_lastWatchdogPoll).count();
            if (watchdogElapsed >= WATCHDOG_INTERVAL_SECONDS)
            {
                m_lastWatchdogPoll = now;

                // Skip watchdog during warmup period
                auto warmupElapsed = std::chrono::duration_cast<std::chrono::seconds>(
                    now - m_launchTime).count();
                if (warmupElapsed < WARMUP_SECONDS)
                    break;

                if (!isTcpsvcsRunning())
                {
                    // All TCPSVCS processes died — relaunch
                    MonitorLog::instance().warn("rndr", "TCPSVCS processes gone, relaunching RNDR");
                    m_hangDetected = false;
                    launchRndr();
                }
                else if (!isTcpsvcsResponsive())
                {
                    // Hung — track grace period
                    if (!m_hangDetected)
                    {
                        m_hangDetected = true;
                        m_hangDetectedTime = now;
                        MonitorLog::instance().warn("rndr", "TCPSVCS appears hung, starting grace period");
                    }
                    else
                    {
                        auto hangElapsed = std::chrono::duration_cast<std::chrono::seconds>(
                            now - m_hangDetectedTime).count();
                        if (hangElapsed >= HANG_GRACE_SECONDS)
                        {
                            MonitorLog::instance().warn("rndr", "TCPSVCS hung for " +
                                std::to_string(hangElapsed) + "s, killing and relaunching");
                            killRndr();
                            m_hangDetected = false;
                            launchRndr();
                        }
                    }
                }
                else
                {
                    // Responsive — reset hang tracking
                    if (m_hangDetected)
                    {
                        MonitorLog::instance().info("rndr", "TCPSVCS recovered from hang");
                        m_hangDetected = false;
                    }
                }
            }
        }
        break;

    case RndrState::RenderActive:
        if (!isRendering)
        {
            // Render finished — start cooldown
            m_cooldownStart = now;
            m_state = RndrState::Cooldown;
            MonitorLog::instance().info("rndr", "Render finished, starting 120s cooldown");
        }
        break;

    case RndrState::Cooldown:
        if (isRendering)
        {
            // New render arrived during cooldown — go back
            m_state = RndrState::RenderActive;
            break;
        }

        if (dccBlocked)
        {
            m_state = RndrState::Inactive;
            break;
        }

        {
            auto cooldownElapsed = std::chrono::duration_cast<std::chrono::seconds>(
                now - m_cooldownStart).count();
            if (cooldownElapsed >= COOLDOWN_SECONDS)
            {
                MonitorLog::instance().info("rndr", "Cooldown elapsed, relaunching RNDR");
                if (isTcpsvcsRunning())
                {
                    // Already running (shouldn't happen, but be safe)
                    m_launchTime = std::chrono::steady_clock::now();
                    m_state = RndrState::Running;
                }
                else if (launchRndr())
                {
                    m_state = RndrState::Running;
                }
            }
        }
        break;
    }
#else
    (void)isRendering;
    (void)dualModeEnabled;
    (void)dccGateEnabled;
    (void)blockProcesses;
#endif
}

void RndrSupervisor::updateDccOccupancy(
    bool gateEnabled,
    const std::vector<std::string>& names,
    std::chrono::steady_clock::time_point now)
{
#ifdef _WIN32
    // Turning the gate on mid-session starts a fresh settle, for the same
    // reason construction does: we don't know what has been running.
    if (gateEnabled && !m_dccGateEnabled)
        m_lastOccupiedSeen = now;
    m_dccGateEnabled = gateEnabled;

    if (!gateEnabled)
    {
        m_dccPresent = false;
        m_dccName.clear();
        return;
    }

    if (std::chrono::duration_cast<std::chrono::seconds>(
            now - m_lastDccPoll).count() < DCC_POLL_SECONDS)
        return;
    m_lastDccPoll = now;

    const std::string found = scanDccPresence(names);
    const bool present = !found.empty();

    if (present)
    {
        m_lastOccupiedSeen = now;
        if (!m_dccPresent || m_dccName != found)
            MonitorLog::instance().info("dcc", "DCC detected: " + found);
        m_dccName = found;
    }
    else if (m_dccPresent)
    {
        MonitorLog::instance().info("dcc", "DCC no longer detected: " + m_dccName +
            " (settling for " + std::to_string(DCC_SETTLE_SECONDS / 60) + " min)");
        m_dccName.clear();
    }

    m_dccPresent = present;
#else
    (void)gateEnabled; (void)names; (void)now;
#endif
}

std::string RndrSupervisor::scanDccPresence(const std::vector<std::string>& names) const
{
#ifdef _WIN32
    if (names.empty())
        return {};

    // Program Files roots, resolved once per scan. A 64-bit process sees
    // both of these unredirected.
    std::wstring roots[2];
    {
        wchar_t buf[MAX_PATH];
        if (GetEnvironmentVariableW(L"ProgramFiles", buf, MAX_PATH) > 0)
            roots[0] = buf;
        if (GetEnvironmentVariableW(L"ProgramFiles(x86)", buf, MAX_PATH) > 0)
            roots[1] = buf;
    }
    auto lower = [](std::wstring v) {
        std::transform(v.begin(), v.end(), v.begin(), ::towlower);
        return v;
    };
    for (auto& r : roots)
    {
        if (r.empty())
            continue;
        if (r.back() != L'\\')
            r.push_back(L'\\');   // so "Program FilesX" can't prefix-match
        r = lower(std::move(r));
    }
    if (roots[0].empty() && roots[1].empty())
        return {};

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return {};

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    std::string hit;

    if (Process32FirstW(snap, &pe))
    {
        do
        {
            // Exact image-name match only — never a prefix. Adobe and
            // others leave permanent background helpers running whose
            // names share a stem with the app itself.
            bool nameMatch = false;
            for (const auto& n : names)
            {
                std::wstring wn(n.begin(), n.end());
                if (_wcsicmp(pe.szExeFile, wn.c_str()) == 0)
                {
                    nameMatch = true;
                    break;
                }
            }
            if (!nameMatch)
                continue;

            HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,
                                      FALSE, pe.th32ProcessID);
            if (!proc)
                continue;   // another user's process — can't verify, don't count

            wchar_t path[MAX_PATH];
            DWORD len = MAX_PATH;
            const bool gotPath = QueryFullProcessImageNameW(proc, 0, path, &len) != 0;
            CloseHandle(proc);
            if (!gotPath)
                continue;

            const std::wstring lowered = lower(std::wstring(path, len));
            for (const auto& r : roots)
            {
                if (!r.empty() && lowered.rfind(r, 0) == 0)
                {
                    std::wstring exe(pe.szExeFile);
                    hit.assign(exe.begin(), exe.end());
                    break;
                }
            }
        } while (hit.empty() && Process32NextW(snap, &pe));
    }

    CloseHandle(snap);
    return hit;
#else
    (void)names;
    return {};
#endif
}

void RndrSupervisor::shutdown()
{
#ifdef _WIN32
    if (m_state != RndrState::Inactive && isTcpsvcsRunning())
    {
        MonitorLog::instance().info("rndr", "Shutting down RNDR client");
        killRndr();
    }
#endif
    m_state = RndrState::Inactive;
    m_hangDetected = false;
}

bool RndrSupervisor::isRndrRunning() const
{
    return m_state == RndrState::Running && isTcpsvcsRunning();
}

std::string RndrSupervisor::statusText() const
{
    switch (m_state)
    {
    case RndrState::Running:
        return isTcpsvcsRunning() ? "Running" : "Starting...";
    case RndrState::RenderActive:
        return "Paused (rendering)";
    case RndrState::Cooldown:
    {
        auto now = std::chrono::steady_clock::now();
        auto remaining = COOLDOWN_SECONDS - std::chrono::duration_cast<std::chrono::seconds>(
            now - m_cooldownStart).count();
        if (remaining < 0) remaining = 0;
        return "Cooldown (" + std::to_string(remaining) + "s)";
    }
    case RndrState::Inactive:
    default:
        if (m_dccGateEnabled && m_dccPresent)
            return "Blocked (" + m_dccName + ")";
        if (m_dccGateEnabled)
        {
            auto now = std::chrono::steady_clock::now();
            auto remaining = DCC_SETTLE_SECONDS
                - std::chrono::duration_cast<std::chrono::seconds>(
                      now - m_lastOccupiedSeen).count();
            if (remaining > 0)
                return "Settling (" + std::to_string((remaining + 59) / 60) + "m)";
        }
        return "Inactive";
    }
}

bool RndrSupervisor::launchRndr()
{
#ifdef _WIN32
    // Don't launch if TCPSVCS is already running
    if (isTcpsvcsRunning())
    {
        MonitorLog::instance().info("rndr", "TCPSVCS already running, skipping launch");
        m_launchTime = std::chrono::steady_clock::now();
        return true;
    }

    std::wstring cmdLine = L"\"" + m_binaryPath + L"\"";

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    BOOL ok = CreateProcessW(
        nullptr,
        cmdLine.data(),
        nullptr, nullptr,
        FALSE,
        CREATE_NO_WINDOW | CREATE_BREAKAWAY_FROM_JOB,
        nullptr, nullptr,
        &si, &pi
    );

    if (!ok)
    {
        MonitorLog::instance().error("rndr", "Failed to launch RNDR: " + std::to_string(GetLastError()));
        return false;
    }

    // rndrclient.exe is a launcher — close handles immediately, it will exit on its own
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    m_launchTime = std::chrono::steady_clock::now();
    m_hangDetected = false;

    MonitorLog::instance().info("rndr", "RNDR client launched (launcher PID=" + std::to_string(pi.dwProcessId) + ")");
    return true;
#else
    return false;
#endif
}

void RndrSupervisor::killRndr()
{
#ifdef _WIN32
    // Kill via taskkill /F /IM TCPSVCS.EXE (fire and forget)
    std::wstring cmdLine = L"taskkill /F /IM TCPSVCS.EXE";

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    BOOL ok = CreateProcessW(
        nullptr,
        cmdLine.data(),
        nullptr, nullptr,
        FALSE,
        CREATE_NO_WINDOW,
        nullptr, nullptr,
        &si, &pi
    );

    if (ok)
    {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }

    MonitorLog::instance().info("rndr", "RNDR kill issued (taskkill TCPSVCS.EXE)");
#endif
}

// Check if any TCPSVCS.EXE process exists (process name enumeration, like PS1 Get-Process)
bool RndrSupervisor::isTcpsvcsRunning() const
{
#ifdef _WIN32
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return false;

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);

    bool found = false;
    if (Process32FirstW(snap, &pe))
    {
        do
        {
            if (_wcsicmp(pe.szExeFile, L"TCPSVCS.EXE") == 0)
            {
                found = true;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }

    CloseHandle(snap);
    return found;
#else
    return false;
#endif
}

// Check if all TCPSVCS windows are responsive (mirrors PS1 .Responding check).
// If no TCPSVCS has visible windows (headless), we assume responsive.
bool RndrSupervisor::isTcpsvcsResponsive() const
{
#ifdef _WIN32
    // First, collect all TCPSVCS PIDs
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return true;

    std::vector<DWORD> pids;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);

    if (Process32FirstW(snap, &pe))
    {
        do
        {
            if (_wcsicmp(pe.szExeFile, L"TCPSVCS.EXE") == 0)
                pids.push_back(pe.th32ProcessID);
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);

    if (pids.empty())
        return true; // No processes, nothing to check

    // Check windows belonging to any TCPSVCS process
    struct EnumData {
        const std::vector<DWORD>* pids;
        bool foundWindow;
        bool allResponsive;
    };
    EnumData data{ &pids, false, true };

    EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
        auto* d = reinterpret_cast<EnumData*>(lParam);
        DWORD windowPid = 0;
        GetWindowThreadProcessId(hwnd, &windowPid);

        for (DWORD pid : *d->pids)
        {
            if (windowPid == pid && IsWindowVisible(hwnd))
            {
                d->foundWindow = true;
                DWORD_PTR result = 0;
                LRESULT lr = SendMessageTimeoutW(hwnd, WM_NULL, 0, 0,
                    SMTO_ABORTIFHUNG, 2000, &result);
                if (lr == 0)
                    d->allResponsive = false;
                break;
            }
        }
        return TRUE; // keep enumerating (check all windows)
    }, reinterpret_cast<LPARAM>(&data));

    // If no visible windows found, assume responsive (headless processes)
    if (!data.foundWindow)
        return true;

    return data.allResponsive;
#else
    return true;
#endif
}

} // namespace MR

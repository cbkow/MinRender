#include "monitor/rndr_supervisor.h"
#include "core/monitor_log.h"

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

void RndrSupervisor::update(bool isRendering, bool dualModeEnabled)
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

    switch (m_state)
    {
    case RndrState::Inactive:
        if (!isRendering)
        {
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
            MonitorLog::instance().info("rndr", "Render finished, starting 30s cooldown");
        }
        break;

    case RndrState::Cooldown:
        if (isRendering)
        {
            // New render arrived during cooldown — go back
            m_state = RndrState::RenderActive;
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

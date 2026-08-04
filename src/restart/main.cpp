#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <string>

// Minimal native restart helper.
// Usage: mr-restart.exe --pid <PID> --exe <path> [--minimized]
//
// 1. Waits up to 15s for the parent process to exit.
// 2. If it doesn't exit, terminates it.
// 3. Sleeps 2s, then relaunches the exe.
//
// Invoked with NO arguments it is the MSIX startup task instead: the
// manifest's windows.startupTask extension cannot pass arguments to its
// Executable, so it points here and this launches the sibling
// minRender.exe with --minimized. (The Inno startup shortcut passes
// --minimized to minRender.exe directly and doesn't use this path.)

static int runStartupTask()
{
    wchar_t self[MAX_PATH] = {};
    DWORD n = GetModuleFileNameW(nullptr, self, MAX_PATH);
    if (n == 0 || n >= MAX_PATH)
        return 1;

    std::wstring exe(self);
    size_t slash = exe.find_last_of(L'\\');
    if (slash == std::wstring::npos)
        return 1;
    exe = exe.substr(0, slash + 1) + L"minRender.exe";

    std::wstring cmdLine = L"\"" + exe + L"\" --minimized";

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};

    if (!CreateProcessW(exe.c_str(), cmdLine.data(),
                        nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi))
        return 1;

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return 0;
}

int main(int argc, char* argv[])
{
    if (argc == 1)
        return runStartupTask();

    DWORD pid = 0;
    std::string exePath;
    bool minimized = false;

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--pid" && i + 1 < argc)
            pid = static_cast<DWORD>(std::stoul(argv[++i]));
        else if (arg == "--exe" && i + 1 < argc)
            exePath = argv[++i];
        else if (arg == "--minimized")
            minimized = true;
    }

    if (pid == 0 || exePath.empty())
        return 1;

    // Wait for parent to exit
    HANDLE hProc = OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE, FALSE, pid);
    if (hProc)
    {
        DWORD waitResult = WaitForSingleObject(hProc, 15000);
        if (waitResult == WAIT_TIMEOUT)
            TerminateProcess(hProc, 1);
        CloseHandle(hProc);
    }

    // Grace period — let file locks release
    Sleep(2000);

    // Relaunch
    std::wstring wExe(exePath.begin(), exePath.end());
    std::wstring cmdLine = L"\"" + wExe + L"\"";
    if (minimized)
        cmdLine += L" --minimized";

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};

    CreateProcessW(
        wExe.c_str(),
        cmdLine.data(),
        nullptr, nullptr, FALSE,
        0,
        nullptr, nullptr,
        &si, &pi);

    if (pi.hProcess) CloseHandle(pi.hProcess);
    if (pi.hThread) CloseHandle(pi.hThread);

    return 0;
}

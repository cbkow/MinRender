#include "core/single_instance.h"
#include "core/platform.h"

#include <iostream>

namespace MR {

#ifdef _WIN32

SingleInstance::SingleInstance(const std::string& name)
{
    // Create a named mutex. If it already exists, GetLastError() == ERROR_ALREADY_EXISTS.
    std::wstring wideName(name.begin(), name.end());
    m_mutex = CreateMutexW(nullptr, FALSE, wideName.c_str());
    m_isFirst = (GetLastError() != ERROR_ALREADY_EXISTS);
}

SingleInstance::~SingleInstance()
{
    if (m_mutex)
    {
        ReleaseMutex(m_mutex);
        CloseHandle(m_mutex);
        m_mutex = nullptr;
    }
}

bool SingleInstance::isFirst() const
{
    return m_isFirst;
}

void SingleInstance::signalExisting()
{
    // Find the tray's message-only HWND and post a custom message
    HWND hwnd = FindWindowExW(HWND_MESSAGE, nullptr, L"MinRenderTray", nullptr);
    if (hwnd)
    {
        // WM_APP + 2 = "show window" signal
        PostMessageW(hwnd, WM_APP + 2, 0, 0);
    }
}

#else // !_WIN32

#include <sys/file.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>

SingleInstance::SingleInstance(const std::string& name)
{
    // Lock file in /tmp
    m_lockPath = "/tmp/" + name + ".lock";
    m_lockFd = open(m_lockPath.c_str(), O_CREAT | O_RDWR, 0600);
    if (m_lockFd < 0)
    {
        m_isFirst = true; // can't determine, assume first
        return;
    }

    // Try exclusive non-blocking lock
    if (flock(m_lockFd, LOCK_EX | LOCK_NB) == 0)
    {
        m_isFirst = true;
    }
    else
    {
        // Another instance holds the lock
        m_isFirst = false;
        close(m_lockFd);
        m_lockFd = -1;
    }
}

SingleInstance::~SingleInstance()
{
    if (m_lockFd >= 0)
    {
        flock(m_lockFd, LOCK_UN);
        close(m_lockFd);
        unlink(m_lockPath.c_str());
    }
}

bool SingleInstance::isFirst() const
{
    return m_isFirst;
}

void SingleInstance::signalExisting()
{
    // On Unix, we don't have a message-only window to signal.
    // The Tauri app handles single-instance via its plugin.
    // For headless, a second launch simply exits.
    std::cout << "[SingleInstance] Another instance is running" << std::endl;
}

#endif

} // namespace MR

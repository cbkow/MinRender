#include "core/ipc_server.h"

#include <iostream>
#include <cstring>

#ifdef _WIN32
#include <sddl.h>

namespace MR {

IpcServer::IpcServer()
{
    m_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    m_connectEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
}

IpcServer::~IpcServer()
{
    close();
    if (m_stopEvent) CloseHandle(m_stopEvent);
    if (m_connectEvent) CloseHandle(m_connectEvent);
}

bool IpcServer::create(const std::string& nodeId)
{
    std::wstring pipeName = L"\\\\.\\pipe\\MinRenderAgent_";
    for (char c : nodeId) pipeName += static_cast<wchar_t>(c);

    // Build DACL restricting pipe to current user
    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = FALSE;
    PSECURITY_DESCRIPTOR pSD = nullptr;
    bool hasDacl = false;

    // Get current user's SID string
    HANDLE token = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
    {
        DWORD tokenLen = 0;
        GetTokenInformation(token, TokenUser, nullptr, 0, &tokenLen);
        if (tokenLen > 0)
        {
            auto* tokenUser = reinterpret_cast<TOKEN_USER*>(new char[tokenLen]);
            if (GetTokenInformation(token, TokenUser, tokenUser, tokenLen, &tokenLen))
            {
                LPWSTR sidStr = nullptr;
                if (ConvertSidToStringSidW(tokenUser->User.Sid, &sidStr))
                {
                    // SDDL: grant Generic All to this user only
                    std::wstring sddl = L"D:(A;;GA;;;" + std::wstring(sidStr) + L")";
                    LocalFree(sidStr);

                    if (ConvertStringSecurityDescriptorToSecurityDescriptorW(
                            sddl.c_str(), SDDL_REVISION_1, &pSD, nullptr))
                    {
                        sa.lpSecurityDescriptor = pSD;
                        hasDacl = true;
                    }
                }
            }
            delete[] reinterpret_cast<char*>(tokenUser);
        }
        CloseHandle(token);
    }

    m_pipe = CreateNamedPipeW(
        pipeName.c_str(),
        PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
        1,          // max instances
        65536,      // out buffer
        65536,      // in buffer
        0,          // default timeout
        hasDacl ? &sa : nullptr
    );

    if (pSD) LocalFree(pSD);

    if (m_pipe == INVALID_HANDLE_VALUE)
    {
        std::cerr << "[IpcServer] CreateNamedPipe failed: " << GetLastError() << std::endl;
        return false;
    }

    ResetEvent(m_stopEvent);

    std::cout << "[IpcServer] Pipe created for node " << nodeId << std::endl;
    return true;
}

bool IpcServer::acceptConnection()
{
    if (m_pipe == INVALID_HANDLE_VALUE) return false;

    ResetEvent(m_connectEvent);

    OVERLAPPED ov{};
    ov.hEvent = m_connectEvent;

    BOOL result = ConnectNamedPipe(m_pipe, &ov);
    if (result)
    {
        // Client already connected (rare but valid)
        m_connected = true;
        std::cout << "[IpcServer] Client connected (immediate)" << std::endl;
        return true;
    }

    DWORD err = GetLastError();
    if (err == ERROR_PIPE_CONNECTED)
    {
        // Client was already waiting before we called ConnectNamedPipe
        m_connected = true;
        std::cout << "[IpcServer] Client connected (already waiting)" << std::endl;
        return true;
    }

    if (err != ERROR_IO_PENDING)
    {
        std::cerr << "[IpcServer] ConnectNamedPipe failed: " << err << std::endl;
        return false;
    }

    // Wait for either client connection or stop signal
    HANDLE events[] = { m_connectEvent, m_stopEvent };
    DWORD waitResult = WaitForMultipleObjects(2, events, FALSE, INFINITE);

    if (waitResult == WAIT_OBJECT_0)
    {
        // Connection event fired
        DWORD bytesTransferred = 0;
        if (GetOverlappedResult(m_pipe, &ov, &bytesTransferred, FALSE))
        {
            m_connected = true;
            std::cout << "[IpcServer] Client connected" << std::endl;
            return true;
        }
        else
        {
            std::cerr << "[IpcServer] GetOverlappedResult failed: " << GetLastError() << std::endl;
            return false;
        }
    }
    else if (waitResult == WAIT_OBJECT_0 + 1)
    {
        // Stop event
        CancelIo(m_pipe);
        std::cout << "[IpcServer] Accept cancelled (stop signaled)" << std::endl;
        return false;
    }
    else
    {
        std::cerr << "[IpcServer] WaitForMultipleObjects failed: " << GetLastError() << std::endl;
        CancelIo(m_pipe);
        return false;
    }
}

bool IpcServer::send(const std::string& json)
{
    if (!m_connected || m_pipe == INVALID_HANDLE_VALUE) return false;

    std::lock_guard<std::mutex> lock(m_writeMutex);

    // Write 4-byte little-endian length prefix
    uint32_t len = static_cast<uint32_t>(json.size());

    if (!writeAll(&len, sizeof(len)))
    {
        std::cerr << "[IpcServer] Failed to write length prefix: " << GetLastError() << std::endl;
        m_connected = false;
        return false;
    }

    // Write payload
    if (!writeAll(json.data(), len))
    {
        std::cerr << "[IpcServer] Failed to write payload: " << GetLastError() << std::endl;
        m_connected = false;
        return false;
    }

    return true;
}

bool IpcServer::writeAll(const void* data, DWORD count)
{
    // Must use overlapped I/O — the pipe was created with FILE_FLAG_OVERLAPPED,
    // so synchronous WriteFile (nullptr OVERLAPPED) is undefined behavior.
    OVERLAPPED ov{};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!ov.hEvent) return false;

    BOOL result = WriteFile(m_pipe, data, count, nullptr, &ov);

    if (result)
    {
        // Completed synchronously — still must use GetOverlappedResult
        // to get reliable byte count on an overlapped handle.
        DWORD transferred = 0;
        GetOverlappedResult(m_pipe, &ov, &transferred, FALSE);
        CloseHandle(ov.hEvent);
        return transferred == count;
    }

    DWORD err = GetLastError();
    if (err != ERROR_IO_PENDING)
    {
        CloseHandle(ov.hEvent);
        if (err == ERROR_BROKEN_PIPE || err == ERROR_NO_DATA)
            m_connected = false;
        return false;
    }

    // Wait for write to complete (5s timeout — local pipe should be instant)
    DWORD waitResult = WaitForSingleObject(ov.hEvent, 5000);
    if (waitResult == WAIT_OBJECT_0)
    {
        DWORD transferred = 0;
        BOOL ok = GetOverlappedResult(m_pipe, &ov, &transferred, FALSE);
        CloseHandle(ov.hEvent);
        if (!ok)
        {
            DWORD ovErr = GetLastError();
            if (ovErr == ERROR_BROKEN_PIPE) m_connected = false;
            return false;
        }
        return transferred == count;
    }

    // Timeout — cancel the pending write
    CancelIo(m_pipe);
    CloseHandle(ov.hEvent);
    return false;
}

bool IpcServer::readExact(void* buf, DWORD count, int timeoutMs)
{
    DWORD totalRead = 0;
    auto* p = static_cast<char*>(buf);

    while (totalRead < count)
    {
        OVERLAPPED ov{};
        ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!ov.hEvent) return false;

        BOOL result = ReadFile(m_pipe, p + totalRead, count - totalRead, nullptr, &ov);

        if (result)
        {
            // Read completed synchronously — still must use GetOverlappedResult
            // to get reliable byte count on an overlapped handle.
            DWORD transferred = 0;
            GetOverlappedResult(m_pipe, &ov, &transferred, FALSE);
            CloseHandle(ov.hEvent);
            totalRead += transferred;
            continue;
        }

        DWORD err = GetLastError();
        if (err != ERROR_IO_PENDING)
        {
            CloseHandle(ov.hEvent);
            if (err == ERROR_BROKEN_PIPE || err == ERROR_NO_DATA)
            {
                m_connected = false;
            }
            return false;
        }

        // Wait for read to complete, stop event, or timeout
        HANDLE events[] = { ov.hEvent, m_stopEvent };
        DWORD timeout = (timeoutMs < 0) ? INFINITE : static_cast<DWORD>(timeoutMs);
        DWORD waitResult = WaitForMultipleObjects(2, events, FALSE, timeout);

        if (waitResult == WAIT_OBJECT_0)
        {
            DWORD transferred = 0;
            if (GetOverlappedResult(m_pipe, &ov, &transferred, FALSE))
            {
                totalRead += transferred;
            }
            else
            {
                DWORD ovErr = GetLastError();
                CloseHandle(ov.hEvent);
                if (ovErr == ERROR_BROKEN_PIPE) m_connected = false;
                return false;
            }
        }
        else
        {
            // Timeout or stop event
            CancelIo(m_pipe);
            CloseHandle(ov.hEvent);
            if (waitResult == WAIT_OBJECT_0 + 1) m_connected = false;
            return false;
        }

        CloseHandle(ov.hEvent);
    }

    return true;
}

std::optional<std::string> IpcServer::receive(int timeoutMs)
{
    if (!m_connected || m_pipe == INVALID_HANDLE_VALUE) return std::nullopt;

    // Read 4-byte length prefix
    uint32_t len = 0;
    if (!readExact(&len, sizeof(len), timeoutMs))
        return std::nullopt;

    // Sanity check
    if (len > 16 * 1024 * 1024)
    {
        std::cerr << "[IpcServer] Message too large: " << len << " bytes" << std::endl;
        m_connected = false;
        return std::nullopt;
    }

    // Read payload
    std::string payload(len, '\0');
    if (!readExact(payload.data(), len, timeoutMs))
    {
        // Protocol desync: length consumed but payload incomplete.
        // Force disconnect so the next reconnection starts clean.
        m_connected = false;
        return std::nullopt;
    }

    return payload;
}

void IpcServer::disconnect()
{
    if (m_pipe != INVALID_HANDLE_VALUE)
    {
        if (m_connected)
            FlushFileBuffers(m_pipe);
        DisconnectNamedPipe(m_pipe);
        m_connected = false;
        std::cout << "[IpcServer] Client disconnected" << std::endl;
    }
}

void IpcServer::close()
{
    signalStop();
    disconnect();
    if (m_pipe != INVALID_HANDLE_VALUE)
    {
        CloseHandle(m_pipe);
        m_pipe = INVALID_HANDLE_VALUE;
        std::cout << "[IpcServer] Pipe closed" << std::endl;
    }
}

void IpcServer::signalStop()
{
    if (m_stopEvent) SetEvent(m_stopEvent);
}

} // namespace MR

#else // !_WIN32 — Unix domain socket implementation

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <cerrno>

namespace MR {

IpcServer::IpcServer()
{
    // Create self-pipe for signaling stop
    if (pipe(m_stopPipe) != 0)
    {
        m_stopPipe[0] = m_stopPipe[1] = -1;
    }
    else
    {
        // Make write end non-blocking
        fcntl(m_stopPipe[1], F_SETFL, O_NONBLOCK);
    }
}

IpcServer::~IpcServer()
{
    close();
    if (m_stopPipe[0] >= 0) ::close(m_stopPipe[0]);
    if (m_stopPipe[1] >= 0) ::close(m_stopPipe[1]);
}

bool IpcServer::create(const std::string& nodeId)
{
    // Socket path: /tmp/minrender-agent-{nodeId}.sock
    m_socketPath = "/tmp/minrender-agent-" + nodeId + ".sock";

    // Remove stale socket file
    ::unlink(m_socketPath.c_str());

    m_listenFd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (m_listenFd < 0)
    {
        std::cerr << "[IpcServer] socket() failed: " << strerror(errno) << std::endl;
        return false;
    }

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, m_socketPath.c_str(), sizeof(addr.sun_path) - 1);

    if (::bind(m_listenFd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0)
    {
        std::cerr << "[IpcServer] bind() failed: " << strerror(errno) << std::endl;
        ::close(m_listenFd);
        m_listenFd = -1;
        return false;
    }

    if (::listen(m_listenFd, 1) != 0)
    {
        std::cerr << "[IpcServer] listen() failed: " << strerror(errno) << std::endl;
        ::close(m_listenFd);
        m_listenFd = -1;
        return false;
    }

    m_stopRequested.store(false);
    std::cout << "[IpcServer] Unix socket created: " << m_socketPath << std::endl;
    return true;
}

bool IpcServer::acceptConnection()
{
    if (m_listenFd < 0) return false;

    // Poll on listen fd + stop pipe
    struct pollfd fds[2];
    fds[0].fd = m_listenFd;
    fds[0].events = POLLIN;
    fds[1].fd = m_stopPipe[0];
    fds[1].events = POLLIN;

    int ret = ::poll(fds, 2, -1); // infinite wait
    if (ret <= 0) return false;

    // Stop signaled?
    if (fds[1].revents & POLLIN)
    {
        std::cout << "[IpcServer] Accept cancelled (stop signaled)" << std::endl;
        return false;
    }

    if (fds[0].revents & POLLIN)
    {
        m_clientFd = ::accept(m_listenFd, nullptr, nullptr);
        if (m_clientFd < 0)
        {
            std::cerr << "[IpcServer] accept() failed: " << strerror(errno) << std::endl;
            return false;
        }
        m_connected.store(true);
        std::cout << "[IpcServer] Client connected" << std::endl;
        return true;
    }

    return false;
}

bool IpcServer::send(const std::string& json)
{
    if (!m_connected.load() || m_clientFd < 0) return false;

    std::lock_guard<std::mutex> lock(m_writeMutex);

    uint32_t len = static_cast<uint32_t>(json.size());
    if (!writeAll(&len, sizeof(len)))
    {
        m_connected.store(false);
        return false;
    }
    if (!writeAll(json.data(), len))
    {
        m_connected.store(false);
        return false;
    }
    return true;
}

std::optional<std::string> IpcServer::receive(int timeoutMs)
{
    if (!m_connected.load() || m_clientFd < 0) return std::nullopt;

    // Read 4-byte length prefix
    uint32_t len = 0;
    if (!readExact(&len, sizeof(len), timeoutMs))
        return std::nullopt;

    if (len > 16 * 1024 * 1024)
    {
        std::cerr << "[IpcServer] Message too large: " << len << " bytes" << std::endl;
        m_connected.store(false);
        return std::nullopt;
    }

    std::string payload(len, '\0');
    if (!readExact(payload.data(), len, timeoutMs))
    {
        m_connected.store(false);
        return std::nullopt;
    }

    return payload;
}

bool IpcServer::readExact(void* buf, size_t count, int timeoutMs)
{
    size_t totalRead = 0;
    auto* p = static_cast<char*>(buf);

    while (totalRead < count)
    {
        // Poll with timeout and stop pipe
        struct pollfd fds[2];
        fds[0].fd = m_clientFd;
        fds[0].events = POLLIN;
        fds[1].fd = m_stopPipe[0];
        fds[1].events = POLLIN;

        int timeout = (timeoutMs < 0) ? -1 : timeoutMs;
        int ret = ::poll(fds, 2, timeout);

        if (ret <= 0) return false; // timeout or error
        if (fds[1].revents & POLLIN) return false; // stop signaled

        if (fds[0].revents & POLLIN)
        {
            ssize_t n = ::read(m_clientFd, p + totalRead, count - totalRead);
            if (n <= 0)
            {
                m_connected.store(false);
                return false;
            }
            totalRead += static_cast<size_t>(n);
        }
    }
    return true;
}

bool IpcServer::writeAll(const void* data, size_t count)
{
    const char* p = static_cast<const char*>(data);
    size_t written = 0;
    while (written < count)
    {
        ssize_t n = ::write(m_clientFd, p + written, count - written);
        if (n <= 0)
        {
            m_connected.store(false);
            return false;
        }
        written += static_cast<size_t>(n);
    }
    return true;
}

void IpcServer::disconnect()
{
    if (m_clientFd >= 0)
    {
        ::close(m_clientFd);
        m_clientFd = -1;
        m_connected.store(false);
        std::cout << "[IpcServer] Client disconnected" << std::endl;
    }
}

void IpcServer::close()
{
    signalStop();
    disconnect();
    if (m_listenFd >= 0)
    {
        ::close(m_listenFd);
        m_listenFd = -1;
    }
    if (!m_socketPath.empty())
    {
        ::unlink(m_socketPath.c_str());
        m_socketPath.clear();
        std::cout << "[IpcServer] Socket closed" << std::endl;
    }
}

void IpcServer::signalStop()
{
    m_stopRequested.store(true);
    if (m_stopPipe[1] >= 0)
    {
        char c = 1;
        ::write(m_stopPipe[1], &c, 1);
    }
}

} // namespace MR

#endif // _WIN32

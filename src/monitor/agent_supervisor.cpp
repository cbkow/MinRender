#include "monitor/agent_supervisor.h"

#include <nlohmann/json.hpp>
#include <filesystem>
#include "core/monitor_log.h"

#ifdef _WIN32
#include <Psapi.h>
#endif

namespace MR {

AgentSupervisor::AgentSupervisor() = default;

AgentSupervisor::~AgentSupervisor()
{
    stop();
}

void AgentSupervisor::start(const std::string& nodeId)
{
    m_nodeId = nodeId;

    if (!m_ipc.create(nodeId))
    {
        MonitorLog::instance().error("agent", "Failed to create IPC pipe");
        return;
    }

    m_running = true;
    m_ipcThread = std::thread(&AgentSupervisor::ipcThreadFunc, this);

    MonitorLog::instance().info("agent", "Started for node " + nodeId);
}

void AgentSupervisor::stop()
{
    if (!m_running) return;

    m_running = false;

    if (m_ipc.isConnected())
    {
        sendJson(R"({"type":"shutdown"})");
    }

    m_ipc.signalStop();

    if (m_ipcThread.joinable())
    {
        m_ipcThread.join();
    }

    m_ipc.close();

#ifdef _WIN32
    if (m_processHandle)
    {
        if (WaitForSingleObject(m_processHandle, 3000) != WAIT_OBJECT_0)
        {
            MonitorLog::instance().warn("agent", "Agent didn't exit in time, terminating");
            TerminateProcess(m_processHandle, 1);
        }
        CloseHandle(m_processHandle);
        m_processHandle = nullptr;
    }
    if (m_threadHandle)
    {
        CloseHandle(m_threadHandle);
        m_threadHandle = nullptr;
    }
#endif

    m_agentPid = 0;
    m_agentState.clear();

    MonitorLog::instance().info("agent", "Stopped");
}

bool AgentSupervisor::spawnAgent()
{
#ifdef _WIN32
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    std::filesystem::path monitorPath(exePath);
    std::filesystem::path agentPath = monitorPath.parent_path() / "mr-agent.exe";

    if (!std::filesystem::exists(agentPath))
    {
        MonitorLog::instance().error("agent", "mr-agent.exe not found at: " + agentPath.string());
        return false;
    }

    std::wstring cmdLine = L"\"" + agentPath.wstring() + L"\" --node-id " +
        std::wstring(m_nodeId.begin(), m_nodeId.end());

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

    if (!ok)
    {
        MonitorLog::instance().error("agent", "CreateProcess failed: " + std::to_string(GetLastError()));
        return false;
    }

    m_processHandle = pi.hProcess;
    m_threadHandle = pi.hThread;
    m_agentPid = pi.dwProcessId;

    MonitorLog::instance().info("agent", "Agent spawned, PID=" + std::to_string(m_agentPid));
    return true;
#else
    MonitorLog::instance().error("agent", "spawnAgent not implemented on this platform");
    return false;
#endif
}

void AgentSupervisor::shutdownAgent()
{
    if (m_ipc.isConnected())
    {
        sendJson(R"({"type":"shutdown"})");
    }

#ifdef _WIN32
    if (m_processHandle)
    {
        if (WaitForSingleObject(m_processHandle, 5000) != WAIT_OBJECT_0)
        {
            MonitorLog::instance().warn("agent", "Agent didn't exit gracefully, terminating");
            TerminateProcess(m_processHandle, 1);
        }

        CloseHandle(m_processHandle);
        m_processHandle = nullptr;
        if (m_threadHandle)
        {
            CloseHandle(m_threadHandle);
            m_threadHandle = nullptr;
        }
    }
#endif

    m_agentPid = 0;
    m_agentState.clear();
    m_ipc.disconnect();

    MonitorLog::instance().info("agent", "Agent shut down");
}

void AgentSupervisor::killAgent()
{
#ifdef _WIN32
    if (m_processHandle)
    {
        TerminateProcess(m_processHandle, 1);
        CloseHandle(m_processHandle);
        m_processHandle = nullptr;
        if (m_threadHandle)
        {
            CloseHandle(m_threadHandle);
            m_threadHandle = nullptr;
        }
    }
#endif

    m_agentPid = 0;
    m_agentState.clear();
    m_ipc.disconnect();

    MonitorLog::instance().info("agent", "Agent killed");
}

bool AgentSupervisor::sendTask(const std::string& taskJson)
{
    return sendJson(taskJson);
}

void AgentSupervisor::setMessageHandler(std::function<void(const std::string&, const nlohmann::json&)> handler)
{
    m_messageHandler = std::move(handler);
}

void AgentSupervisor::sendAbort(const std::string& reason)
{
    nlohmann::json j = {{"type", "abort"}, {"reason", reason}};
    sendJson(j.dump());
}

bool AgentSupervisor::sendJson(const std::string& json)
{
    return m_ipc.send(json);
}

void AgentSupervisor::ipcThreadFunc()
{
    while (m_running)
    {
        try
        {
            if (!m_ipc.acceptConnection())
            {
                if (!m_running) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                continue;
            }

            MonitorLog::instance().info("agent", "Agent connected via IPC");
            m_agentHealth.store(AgentHealth::Ok);

            while (m_running && m_ipc.isConnected())
            {
                auto msg = m_ipc.receive(1000);
                if (msg.has_value())
                {
                    std::lock_guard<std::mutex> lock(m_queueMutex);
                    m_messageQueue.push(std::move(msg.value()));
                }
            }

            MonitorLog::instance().info("agent", "Agent disconnected (render cycle complete)");
            m_ipc.disconnect();

#ifdef _WIN32
            if (m_processHandle)
            {
                DWORD exitCode = 0;
                if (GetExitCodeProcess(m_processHandle, &exitCode) && exitCode != STILL_ACTIVE)
                {
                    CloseHandle(m_processHandle);
                    m_processHandle = nullptr;
                    if (m_threadHandle)
                    {
                        CloseHandle(m_threadHandle);
                        m_threadHandle = nullptr;
                    }
                    // Don't clear m_agentPid / m_agentState here — queued
                    // messages (Completed, Status) may not have been processed
                    // yet by the main thread.  Clearing now would race with
                    // processMessages() and break the readyForWork transition.
                }
            }
#endif
        }
        catch (const std::exception& e)
        {
            MonitorLog::instance().error("agent", std::string("IPC thread exception: ") + e.what());
            m_ipc.disconnect();
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
        catch (...)
        {
            MonitorLog::instance().error("agent", "IPC thread unknown exception");
            m_ipc.disconnect();
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
    }
}

void AgentSupervisor::processMessages()
{
    std::queue<std::string> messages;
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        std::swap(messages, m_messageQueue);
    }

    while (!messages.empty())
    {
        const auto& raw = messages.front();

        try
        {
            auto j = nlohmann::json::parse(raw);
            std::string type = j.value("type", "");

            if (type == "status")
            {
                std::string prevState = m_agentState;
                m_agentState = j.value("state", "unknown");
                uint32_t pid = j.value("pid", 0u);
                if (pid != 0) m_agentPid = pid;
                MonitorLog::instance().info("agent", "Agent status: state=" + m_agentState + " pid=" + std::to_string(m_agentPid));

                // DCC confirmed dead → node is ready for new work
                if (m_agentState == "idle" && prevState == "rendering")
                    m_readyForWork.store(true);
            }
            else if (type == "pong")
            {
                // Agent is alive
            }
            else if (m_messageHandler)
            {
                m_messageHandler(type, j);
            }
            else
            {
                MonitorLog::instance().info("agent", "Received: " + type);
            }
        }
        catch (const std::exception& e)
        {
            MonitorLog::instance().error("agent", std::string("Failed to parse message: ") + e.what());
        }

        messages.pop();
    }

}

bool AgentSupervisor::isAgentRunning() const
{
#ifdef _WIN32
    if (!m_processHandle) return false;
    DWORD exitCode = 0;
    if (GetExitCodeProcess(m_processHandle, &exitCode))
        return exitCode == STILL_ACTIVE;
    return false;
#else
    return false;
#endif
}

std::string AgentSupervisor::agentHealth() const
{
    switch (m_agentHealth.load())
    {
        case AgentHealth::NeedsAttention:  return "needs_attention";
        default:                           return "ok";
    }
}

void AgentSupervisor::resetHealth()
{
    m_agentHealth.store(AgentHealth::Ok);
}

} // namespace MR

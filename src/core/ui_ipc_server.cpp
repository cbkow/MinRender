#include "core/ui_ipc_server.h"
#include "core/monitor_log.h"

#include <chrono>
#include <iostream>

namespace MR {

UiIpcServer::UiIpcServer() = default;

UiIpcServer::~UiIpcServer()
{
    stop();
}

bool UiIpcServer::start(const std::string& nodeId)
{
    if (m_running) return false;

    m_nodeId = nodeId;

    // Create the pipe with a UI-specific name.
    // IpcServer::create() uses "MinRenderAgent_" prefix, but we need a different
    // pipe name. We'll work around this by closing any existing pipe and creating
    // a new one directly. For now, we reuse IpcServer but with a modified node ID
    // that produces the correct pipe name.
    //
    // Pipe name will be: \\.\pipe\MinRenderAgent_ui_{nodeId}
    // TODO: refactor IpcServer to accept a full pipe name, or add a prefix parameter.
    std::string uiPipeId = "ui_" + nodeId;
    if (!m_ipc.create(uiPipeId))
    {
        std::cerr << "[UiIpc] Failed to create pipe for " << nodeId << std::endl;
        return false;
    }

    m_running = true;
    m_thread = std::thread(&UiIpcServer::threadFunc, this);

    MonitorLog::instance().info("ui-ipc", "Server started for node " + nodeId);
    return true;
}

void UiIpcServer::stop()
{
    if (!m_running) return;

    m_running = false;
    m_ipc.signalStop();

    if (m_thread.joinable())
        m_thread.join();

    m_ipc.close();

    // Drain the push queue
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        while (!m_pushQueue.empty())
            m_pushQueue.pop();
    }

    MonitorLog::instance().info("ui-ipc", "Server stopped");
}

void UiIpcServer::push(const std::string& json)
{
    if (!m_running || !m_ipc.isConnected())
        return;

    std::lock_guard<std::mutex> lock(m_queueMutex);

    // Cap queue size to prevent unbounded growth if UI is slow
    if (m_pushQueue.size() < 1000)
        m_pushQueue.push(json);
}

void UiIpcServer::setCommandHandler(std::function<void(const std::string& json)> handler)
{
    std::lock_guard<std::mutex> lock(m_handlerMutex);
    m_commandHandler = std::move(handler);
}

void UiIpcServer::threadFunc()
{
    while (m_running)
    {
        try
        {
            // Wait for a UI client to connect
            if (!m_ipc.acceptConnection())
            {
                if (!m_running) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                continue;
            }

            MonitorLog::instance().info("ui-ipc", "UI client connected");

            // Connected loop: flush push queue + poll for incoming commands
            while (m_running && m_ipc.isConnected())
            {
                // Flush any queued push messages
                flushQueue();

                // Poll for incoming command (short timeout so we can flush frequently)
                auto msg = m_ipc.receive(100); // 100ms timeout
                if (msg.has_value())
                {
                    std::lock_guard<std::mutex> lock(m_handlerMutex);
                    if (m_commandHandler)
                        m_commandHandler(msg.value());
                }
            }

            MonitorLog::instance().info("ui-ipc", "UI client disconnected");

            // Drain queue — stale messages are meaningless for the next client
            {
                std::lock_guard<std::mutex> lock(m_queueMutex);
                while (!m_pushQueue.empty())
                    m_pushQueue.pop();
            }

            m_ipc.disconnect();
        }
        catch (const std::exception& e)
        {
            std::cerr << "[UiIpc] Error in thread: " << e.what() << std::endl;
            m_ipc.disconnect();
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
}

void UiIpcServer::flushQueue()
{
    // Swap the queue out under lock, then send without holding the lock
    std::queue<std::string> batch;
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        batch.swap(m_pushQueue);
    }

    while (!batch.empty() && m_ipc.isConnected())
    {
        if (!m_ipc.send(batch.front()))
        {
            // Send failed — client likely disconnected.
            // Remaining messages are lost (acceptable for push events).
            break;
        }
        batch.pop();
    }
}

} // namespace MR

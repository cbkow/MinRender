#pragma once

#include "core/ipc_server.h"

#include <string>
#include <queue>
#include <mutex>
#include <thread>
#include <atomic>
#include <functional>

namespace MR {

/// IPC server for the Tauri UI process.
/// Pushes state/event messages to the connected UI client.
/// Accepts commands back from the UI (e.g. get_state, get_chunks).
///
/// Wire protocol: same as IpcServer — 4-byte LE length prefix + JSON.
/// Pipe name: \\.\pipe\MinRenderUI_{nodeId}  (Windows)
///            /tmp/minrender-ui-{nodeId}.sock (macOS/Linux, future)
///
/// Single client at a time. If the UI disconnects, the server re-enters
/// accept mode. Queued push messages are discarded on disconnect.
class UiIpcServer
{
public:
    UiIpcServer();
    ~UiIpcServer();

    UiIpcServer(const UiIpcServer&) = delete;
    UiIpcServer& operator=(const UiIpcServer&) = delete;

    /// Start the server: create the pipe and spawn the accept/read thread.
    bool start(const std::string& nodeId);

    /// Stop the server: signal the thread, close the pipe.
    void stop();

    /// Push a JSON message to the connected UI client.
    /// Thread-safe. Messages are queued and flushed by the background thread.
    /// If no client is connected, the message is silently dropped.
    void push(const std::string& json);

    /// Set handler for incoming commands from the UI.
    /// Called on the background thread — keep it fast or queue work.
    void setCommandHandler(std::function<void(const std::string& json)> handler);

    /// Check if a UI client is currently connected.
    bool isConnected() const { return m_ipc.isConnected(); }

private:
    void threadFunc();
    void flushQueue();

    IpcServer m_ipc;
    std::string m_nodeId;

    std::thread m_thread;
    std::atomic<bool> m_running{false};

    // Push queue (main thread writes, background thread reads + sends)
    std::queue<std::string> m_pushQueue;
    std::mutex m_queueMutex;

    // Command handler (called on background thread)
    std::function<void(const std::string&)> m_commandHandler;
    std::mutex m_handlerMutex;
};

} // namespace MR

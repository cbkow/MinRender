#pragma once

#include <functional>
#include <memory>
#include <string>

class QLockFile;
class QLocalServer;

namespace MR {

// Cross-platform single-instance guard. On Windows it uses a named pipe
// under the hood; on Unix a lock file in the temp dir plus a local socket.
//
// Usage from a second launch:
//     MR::SingleInstance si("MinRenderMonitor");
//     if (!si.isFirst()) { si.signalExisting(); return 0; }
//
// First instance (GUI) should call setActivationCallback(...) to be told
// when another launch asks it to come forward. The callback fires on the
// Qt event-loop thread; requires a running QCoreApplication.
class SingleInstance
{
public:
    explicit SingleInstance(const std::string& name);
    ~SingleInstance();

    SingleInstance(const SingleInstance&) = delete;
    SingleInstance& operator=(const SingleInstance&) = delete;

    bool isFirst() const;

    // Called by a second instance: pokes the first to come to the foreground,
    // then returns. No-op if this is itself the first instance.
    void signalExisting();

    // GUI-only: register a handler fired when another process calls
    // signalExisting(). No-op if this is not the first instance.
    void setActivationCallback(std::function<void()> cb);

private:
    std::string m_name;
    std::unique_ptr<QLockFile> m_lock;
    std::unique_ptr<QLocalServer> m_server;
    std::function<void()> m_activationCb;
    bool m_isFirst = false;
};

} // namespace MR

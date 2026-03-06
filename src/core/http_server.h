#pragma once

#include <httplib.h>
#include <thread>
#include <atomic>
#include <cstdint>
#include <string>

namespace MR {

class MonitorApp; // forward

class HttpServer
{
public:
    void init(MonitorApp* app);

    // Set the API secret for bearer token auth. Must be called before start().
    void setApiSecret(const std::string& secret) { m_apiSecret = secret; }

    // Bind to address:port and launch background thread. Returns false if bind fails.
    bool start(const std::string& bindAddress, uint16_t port);
    void stop();

    bool isRunning() const { return m_running.load(); }
    uint16_t boundPort() const { return m_port; }

private:
    void setupRoutes();
    bool requireLeader(httplib::Response& res);

    httplib::Server m_server;
    std::thread m_thread;
    std::atomic<bool> m_running{false};
    MonitorApp* m_app = nullptr;
    uint16_t m_port = 0;
    std::string m_apiSecret;
};

// Build Authorization header for HTTP client calls
inline httplib::Headers authHeaders(const std::string& secret)
{
    if (secret.empty())
        return {};
    return {{"Authorization", "Bearer " + secret}};
}

} // namespace MR

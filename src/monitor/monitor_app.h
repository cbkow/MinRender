#pragma once

#include "core/config.h"
#include "core/node_identity.h"
#include "core/peer_info.h"
#include "core/http_server.h"
#include "core/udp_notify.h"
#include "monitor/agent_supervisor.h"
#include "monitor/render_coordinator.h"
#include "monitor/template_manager.h"
#include "monitor/peer_manager.h"
#include "monitor/database_manager.h"
#include "monitor/dispatch_manager.h"
#include "monitor/submission_watcher.h"

#include "core/system_tray.h"

#include <atomic>
#include <filesystem>
#include <functional>
#include <queue>
#include <string>
#include <thread>
#include <vector>
#include <mutex>

namespace MR {

enum class NodeState { Active, Stopped };

class MonitorApp
{
public:
    bool init();
    void update();
    void shutdown();

    // Config accessors
    Config& config() { return m_config; }
    const Config& config() const { return m_config; }
    const NodeIdentity& identity() const { return m_identity; }
    AgentSupervisor& agentSupervisor() { return m_agentSupervisor; }
    RenderCoordinator& renderCoordinator() { return m_renderCoordinator; }
    PeerManager& peerManager() { return m_peerManager; }
    DatabaseManager& databaseManager() { return m_databaseManager; }
    DispatchManager& dispatchManager() { return m_dispatchManager; }

    // Build this node's PeerInfo (called by HttpServer for GET /api/status)
    PeerInfo buildLocalPeerInfo() const;

    // Cached snapshots for UI
    const std::vector<JobInfo>& cachedJobs() const { return m_cachedJobs; }
    const std::vector<JobTemplate>& cachedTemplates() const { return m_cachedTemplates; }

    // Thread-safe cached JSON for HTTP handlers
    std::string getCachedJobsJson() const;
    std::string getCachedJobDetailJson(const std::string& jobId) const;

    // Chunk data access (leader: DB query, worker: HTTP GET from leader)
    std::vector<ChunkRow> getChunksForJob(const std::string& jobId);

    // Job controls
    void pauseJob(const std::string& jobId);
    void resumeJob(const std::string& jobId);
    void cancelJob(const std::string& jobId);
    void requeueJob(const std::string& jobId);
    void deleteJob(const std::string& jobId);
    void archiveJob(const std::string& jobId);
    void retryFailedChunks(const std::string& jobId);
    void reassignChunk(int64_t chunkId, const std::string& targetNodeId = {});
    std::string resubmitJob(const std::string& jobId);
    std::string resubmitChunkAsJob(const std::string& jobId,
                                    int frameStart, int frameEnd, int chunkSize);
    std::string resubmitIncomplete(const std::string& jobId);
    void unsuspendNode(const std::string& nodeId);

    // Peer remote-control actions. Each looks up the peer's endpoint
    // from PeerManager and queues an HTTP POST on the shared worker
    // thread (no blocking from callers). No-op if the peer is unknown
    // or has no endpoint. setPeerNodeActive also applies the new state
    // locally on PeerManager so the UI reflects the change before the
    // peer's status poll confirms it.
    void setPeerNodeActive(const std::string& nodeId, bool active);
    void restartPeerApp(const std::string& nodeId);

    // Dead-peer fallback: write an empty file at
    // {farmPath}/nodes/{nodeId}/restart so the peer's own filesystem
    // watcher triggers a local restart next time it comes back. Returns
    // false if the farm isn't running or the write failed.
    bool writePeerRestartSignal(const std::string& nodeId);

    // Remove a peer's endpoint.json from the shared farm so
    // PeerManager's stale-peer reaper drops the cached record on its
    // next discovery pass. Used to clean up a duplicate peer row
    // caused by a prior install writing a second endpoint under a
    // different node_id. Returns false if the farm isn't running or
    // the file didn't exist (the latter is also the desired end
    // state, so callers shouldn't treat it as failure).
    bool forgetPeer(const std::string& nodeId);

    // Farm cleanup — shared implementation used by both /api/farm/*
    // endpoints and the Qt FarmCleanupDialog. scanFarmCleanup walks
    // the shared filesystem + DB + peer list and returns six groups:
    //   finished_jobs, archived_jobs, orphaned_dirs, stale_peers,
    //   stale_staging_dirs, failed_staging_copies
    // Each group item is {id, label, detail}. executeFarmCleanup
    // applies one of the actions (archive / delete_jobs / delete_dirs /
    // remove_peers) to the given ids and returns the count processed.
    nlohmann::json scanFarmCleanup() const;
    int executeFarmCleanup(const std::string& action,
                           const std::vector<std::string>& ids);

    // Node state controls
    void setNodeState(NodeState state);
    NodeState nodeState() const { return m_nodeState; }

    // Async HTTP to leader (fire-and-forget or with callback)
    void postToLeaderAsync(const std::string& endpoint, const std::string& body,
                           std::function<void(bool success, const std::string& response)> callback = nullptr,
                           const std::string& method = "POST");

    // Leader election (delegated to PeerManager)
    bool isLeader() const { return m_peerManager.isLeader(); }
    std::string getLeaderEndpoint() const;

    // Tray state
    // trayIconName() returns "green" | "blue" | "yellow" | "red" | "gray" for
    // external consumers (e.g. macOS Swift companion via IPC). The Windows
    // tray itself uses a single icon and surfaces status via trayStatusText().
    std::string trayIconName() const;
    std::string trayTooltip() const;
    std::string trayStatusText() const;

    // Restart (sidecar batch script that relaunches the app)
    bool launchRestartSidecar();

    // Exit flow
    void requestExit();
    bool isExitPending() const { return m_exitRequested && !m_shouldExit; }
    bool shouldExit() const { return m_shouldExit; }
    void beginForceExit();
    void cancelExit();

    void saveConfig();

    // Farm lifecycle
    bool startFarm();
    void stopFarm();
    void requestFarmRestart() { m_farmRestartRequested.store(true); }
    bool isFarmRunning() const { return m_farmRunning; }
    const std::filesystem::path& farmPath() const { return m_farmPath; }
    bool hasFarmError() const { return !m_farmError.empty(); }
    const std::string& farmError() const { return m_farmError; }
    const std::string& farmSecret() const { return m_farmSecret; }

    // Job selection state
    void selectJob(const std::string& id);
    void requestSubmissionMode();
    const std::string& selectedJobId() const { return m_selectedJobId; }
    bool shouldEnterSubmission();

private:
    void loadConfig();
    void onBecomeLeader();
    void onLoseLeadership();
    void refreshCachedJobs();
    void reportCompletion(const std::string& jobId, const ChunkRange& chunk,
                          const std::string& state);
    void reportFrameCompletion(const std::string& jobId, int frame);
    void handleUdpMessages();
    void sendUdpHeartbeat();

    // Background HTTP worker
    void startHttpWorker();
    void stopHttpWorker();
    void httpWorkerLoop();
    bool flushCompletionReports();
    bool flushFrameReports();

    std::filesystem::path m_appDataDir;
    std::filesystem::path m_configPath;

    NodeIdentity m_identity;
    Config m_config;
    AgentSupervisor m_agentSupervisor;
    RenderCoordinator m_renderCoordinator;
    TemplateManager m_templateManager;
    HttpServer m_httpServer;
    PeerManager m_peerManager;
    DatabaseManager m_databaseManager;
    DispatchManager m_dispatchManager;
    SubmissionWatcher m_submissionWatcher;
    UdpNotify m_udpNotify;

    // Cached snapshots
    std::vector<JobInfo> m_cachedJobs;
    std::vector<JobTemplate> m_cachedTemplates;

    // Thread-safe cached JSON for HTTP handlers
    mutable std::mutex m_cachedJsonMutex;
    std::string m_cachedJobsJson = "[]";

    // Farm state
    std::filesystem::path m_farmPath;
    std::string m_farmError;
    std::string m_farmSecret;
    bool m_farmRunning = false;
    NodeState m_nodeState = NodeState::Active;

    // Leader tracking
    bool m_wasLeader = false;
    std::atomic<bool> m_leaderDbReady{false};
    std::thread m_leaderThread;

    // Leader contact cooldown — after any failed HTTP to leader, skip for 5s
    std::chrono::steady_clock::time_point m_leaderContactCooldown;

    // Pending completion reports (worker -> leader, buffered on failure)
    struct PendingReport
    {
        std::string jobId;
        int frameStart = 0, frameEnd = 0;
        std::string state;
        int64_t elapsedMs = 0;
        int exitCode = 0;
        std::string error;
    };
    std::vector<PendingReport> m_pendingReports;

    // Pending frame completion reports (worker -> leader)
    struct PendingFrameReport
    {
        std::string jobId;
        int frame = 0;
    };
    std::vector<PendingFrameReport> m_pendingFrameReports;

    // Thread-safe access to pending reports (worker thread flushes them)
    std::mutex m_reportMutex;

    // Async HTTP queue for one-off requests to leader
    struct HttpRequest {
        std::string host;
        int port = 0;
        std::string method = "POST";
        std::string endpoint;
        std::string body;
        std::function<void(bool success, const std::string& response)> callback;
    };
    std::mutex m_httpQueueMutex;
    std::queue<HttpRequest> m_httpQueue;
    std::thread m_httpWorkerThread;
    std::atomic<bool> m_httpWorkerRunning{false};

    // UDP heartbeat timing
    std::chrono::steady_clock::time_point m_lastUdpHeartbeat;

    // Job cache refresh timing
    std::chrono::steady_clock::time_point m_lastJobCacheRefresh;

    // Job selection
    std::string m_selectedJobId;
    bool m_requestSubmission = false;

    // Deferred farm restart (set by HTTP handler, processed by update())
    std::atomic<bool> m_farmRestartRequested{false};

    // Exit state
    bool m_exitRequested = false;
    bool m_shouldExit = false;

    // Deferred restart — gives sidecar time to start before we exit
    bool m_restartPending = false;
    std::chrono::steady_clock::time_point m_restartLaunchedAt;
};

} // namespace MR

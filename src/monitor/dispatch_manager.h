#pragma once

#include "core/job_types.h"
#include "core/peer_info.h"
#include "monitor/node_failure_tracker.h"

#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>

namespace MR {

class MonitorApp;
class DatabaseManager;

struct CompletionReport
{
    std::string node_id, job_id;
    int frame_start = 0, frame_end = 0;
    int64_t elapsed_ms = 0;
    int exit_code = 0;
};

struct FailureReport
{
    std::string node_id, job_id;
    int frame_start = 0, frame_end = 0;
    std::string error;
};

struct FrameReport
{
    std::string node_id, job_id;
    int frame = 0;
};

struct RevertRequest
{
    std::string job_id;
    int frame_start = 0;
    int frame_end = 0;
};

struct SubmitRequest
{
    JobManifest manifest;
    int priority = 50;
};

class DispatchManager
{
public:
    void init(MonitorApp* app, DatabaseManager* db);

    // Main thread, self-throttled to ~2s
    void update();

    // Thread-safe queues (HTTP handlers -> main thread)
    void queueCompletion(CompletionReport report);
    void queueFailure(FailureReport report);
    void queueSubmission(SubmitRequest request);
    void queueFrameCompletion(FrameReport report);
    void queueRevert(RevertRequest request);

    // Direct submission (main thread, for local leader submit)
    std::string submitJob(const JobManifest& manifest, int priority);

    // Retry only failed chunks (preserves completed work, keeps blacklist)
    bool retryFailedChunks(const std::string& jobId);

    // Create a new job from an existing job's manifest (clean slate)
    std::string resubmitJob(const std::string& sourceJobId);

    // Resubmit with overridden frame range and chunk size
    std::string resubmitJob(const std::string& sourceJobId,
                            int frameStart, int frameEnd, int chunkSize);

    // Submit a job with caller-provided chunk ranges (instead of computeChunks)
    std::string submitJobWithChunks(const JobManifest& manifest, int priority,
                                    const std::vector<ChunkRange>& chunks);

    // Resubmit only incomplete chunks — completed chunks are pre-filled as green
    std::string resubmitIncomplete(const std::string& sourceJobId);

    // Machine-level failure tracking
    NodeFailureTracker& failureTracker() { return m_failureTracker; }

private:
    void processSubmissions();
    void processReports();
    void detectDeadWorkers();
    void checkJobCompletions();
    void assignWork();
    void doSnapshot();
    void tryRemoteAgentRestart(const PeerInfo& peer);

    MonitorApp* m_app = nullptr;
    DatabaseManager* m_db = nullptr;

    std::chrono::steady_clock::time_point m_lastDispatch;
    std::chrono::steady_clock::time_point m_lastSnapshot;

    // Thread-safe queues
    std::mutex m_queueMutex;
    std::queue<CompletionReport> m_completionQueue;
    std::queue<FailureReport> m_failureQueue;
    std::queue<SubmitRequest> m_submitQueue;
    std::queue<FrameReport> m_frameQueue;
    std::queue<RevertRequest> m_revertQueue;

    NodeFailureTracker m_failureTracker;

    std::atomic<bool> m_snapshotInProgress{false};

    // Nodes we've dispatched to that haven't yet shown up as "rendering" in a peer snapshot.
    // Prevents re-assigning work before the UDP heartbeat propagates the busy state.
    std::unordered_set<std::string> m_dispatchedNodes;

    std::unordered_map<std::string, std::chrono::steady_clock::time_point> m_lastAgentRestartAttempt;

    static constexpr int DISPATCH_INTERVAL_MS = 2000;
    static constexpr int SNAPSHOT_INTERVAL_MS = 30000;
    static constexpr int AGENT_RESTART_COOLDOWN_MS = 60000;
};

} // namespace MR

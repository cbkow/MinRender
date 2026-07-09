#pragma once

#include "core/job_types.h"

#include <SQLiteCpp/SQLiteCpp.h>
#include <SQLiteCpp/Backup.h>

#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace MR {

struct ChunkRow
{
    int64_t id = 0;
    std::string job_id;
    int frame_start = 0, frame_end = 0;
    std::string state = "pending";   // pending | assigned | completed | failed
    std::string assigned_to;
    int64_t assigned_at_ms = 0, completed_at_ms = 0;
    int retry_count = 0;
    std::vector<int> completed_frames;  // individual frames completed within this chunk
    std::vector<std::string> failed_on; // node IDs that failed this chunk (blacklist)
};

struct JobRow
{
    std::string job_id;
    std::string manifest_json;       // full JobManifest as JSON string
    std::string current_state = "active";  // active | paused | cancelled | completed
    int priority = 50;
    int64_t submitted_at_ms = 0;
};

struct JobProgress
{
    int total = 0, completed = 0, failed = 0, rendering = 0, pending = 0;
    // Wall-clock duration endpoints, aggregated over the job's chunks.
    // 0 = no chunk assigned / completed yet.
    int64_t first_assigned_ms = 0;
    int64_t last_completed_ms = 0;
};

struct JobSummary
{
    JobRow job;
    JobProgress progress;
};

class DatabaseManager
{
public:
    bool open(const std::filesystem::path& dbPath);
    void close();
    bool isOpen() const { return m_db != nullptr; }

    // Jobs
    bool insertJob(const JobRow& job);
    std::optional<JobRow> getJob(const std::string& jobId);
    std::vector<JobSummary> getAllJobs();
    bool updateJobState(const std::string& jobId, const std::string& newState);
    bool updateJobPriority(const std::string& jobId, int priority);
    // Reorder jobId next to targetJobId within their (equal) priority group.
    // Fails if priorities differ — drag-reorder is within-group only.
    bool moveJob(const std::string& jobId, const std::string& targetJobId, bool before);
    bool deleteJob(const std::string& jobId);

    // Chunks
    bool insertChunks(const std::string& jobId, const std::vector<ChunkRange>& chunks);
    std::vector<ChunkRow> getChunksForJob(const std::string& jobId);

    // Dispatch operations
    std::optional<std::pair<ChunkRow, std::string/*manifest_json*/>>
        findNextPendingChunk();
    std::optional<std::pair<ChunkRow, std::string/*manifest_json*/>>
        findNextPendingChunkForNode(const std::vector<std::string>& nodeTags,
                                    const std::string& nodeId = {});
    bool assignChunk(int64_t chunkId, const std::string& nodeId, int64_t nowMs);
    bool completeChunk(const std::string& jobId, int frameStart, int frameEnd, int64_t nowMs);
    bool failChunk(const std::string& jobId, int frameStart, int frameEnd,
                   int maxRetries, const std::string& failingNodeId = {});
    bool revertChunkToPending(const std::string& jobId, int frameStart, int frameEnd);
    int reassignDeadWorkerChunks(const std::string& deadNodeId);
    std::string getJobCompletionState(const std::string& jobId);
    bool resetAllChunks(const std::string& jobId);

    // --- Job editing ---
    bool updateJobManifest(const std::string& jobId, const std::string& manifestJson);
    // Nodes currently rendering this job (distinct assigned_to of
    // 'assigned' chunks) — collected before a reset so the caller can
    // push aborts to them.
    std::vector<std::string> getAssignedNodes(const std::string& jobId);
    // Return in-flight chunks to the pool; completed/failed are untouched.
    bool resetAssignedChunks(const std::string& jobId);
    // Drop every chunk row and insert a fresh set (start over with a
    // new frame range / chunk size).
    bool rebuildChunks(const std::string& jobId, const std::vector<ChunkRange>& chunks);
    bool retryFailedChunks(const std::string& jobId);
    bool reassignChunk(int64_t chunkId, const std::string& targetNodeId = {});
    void markChunksCompleted(const std::string& jobId, const std::vector<ChunkRange>& ranges);

    // Per-frame completion tracking
    bool addCompletedFrames(const std::string& jobId, int frame);
    bool addCompletedFramesBatch(const std::string& jobId, const std::vector<int>& frames);

    // Snapshot for failover recovery
    bool snapshotTo(const std::filesystem::path& destPath);
    bool restoreFrom(const std::filesystem::path& snapshotPath,
                     const std::filesystem::path& localPath);

private:
    void createSchema();
    std::unique_ptr<SQLite::Database> m_db;
    std::filesystem::path m_dbPath;
    mutable std::recursive_mutex m_mutex;
};

} // namespace MR

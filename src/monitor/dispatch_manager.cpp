#include "monitor/dispatch_manager.h"
#include "monitor/database_manager.h"
#include "monitor/monitor_app.h"
#include "core/monitor_log.h"
#include "core/net_utils.h"
#include "core/http_server.h"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <thread>
#include <unordered_map>

namespace MR {

void DispatchManager::init(MonitorApp* app, DatabaseManager* db)
{
    m_app = app;
    m_db = db;
    m_lastDispatch = std::chrono::steady_clock::now();
    m_lastSnapshot = std::chrono::steady_clock::now();
}

void DispatchManager::update()
{
    if (!m_app || !m_db || !m_db->isOpen())
        return;

    auto now = std::chrono::steady_clock::now();
    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastDispatch).count();
    if (elapsedMs < DISPATCH_INTERVAL_MS)
        return;
    m_lastDispatch = now;

    // 1. Drain submit queue
    processSubmissions();

    // 2. Drain completion + failure queues
    processReports();

    // 3. Detect dead workers and reassign their chunks
    detectDeadWorkers();

    // 4. Check if any active jobs are now complete
    checkJobCompletions();

    // 5. Assign work to idle workers
    assignWork();

    // 6. Periodic snapshot
    auto snapshotElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - m_lastSnapshot).count();
    if (snapshotElapsed >= SNAPSHOT_INTERVAL_MS)
    {
        doSnapshot();
        m_lastSnapshot = now;
    }
}

// --- Thread-safe queues ---

void DispatchManager::queueCompletion(CompletionReport report)
{
    std::lock_guard<std::mutex> lock(m_queueMutex);
    m_completionQueue.push(std::move(report));
}

void DispatchManager::queueFailure(FailureReport report)
{
    std::lock_guard<std::mutex> lock(m_queueMutex);
    m_failureQueue.push(std::move(report));
}

void DispatchManager::queueSubmission(SubmitRequest request)
{
    std::lock_guard<std::mutex> lock(m_queueMutex);
    m_submitQueue.push(std::move(request));
}

void DispatchManager::queueFrameCompletion(FrameReport report)
{
    std::lock_guard<std::mutex> lock(m_queueMutex);
    m_frameQueue.push(std::move(report));
}

void DispatchManager::queueRevert(RevertRequest request)
{
    std::lock_guard<std::mutex> lock(m_queueMutex);
    m_revertQueue.push(std::move(request));
}

// --- Direct submission ---

std::string DispatchManager::submitJob(const JobManifest& manifest, int priority)
{
    if (!m_db || !m_db->isOpen())
        return {};

    // Insert job row
    JobRow row;
    row.job_id = manifest.job_id;
    row.manifest_json = nlohmann::json(manifest).dump();
    row.current_state = "active";
    row.priority = priority;
    row.submitted_at_ms = manifest.submitted_at_ms;

    if (!m_db->insertJob(row))
        return {};

    // Create output directory once (leader-only, at submission time)
    if (manifest.output_dir.has_value() && !manifest.output_dir.value().empty())
    {
        std::error_code ec;
        std::filesystem::create_directories(manifest.output_dir.value(), ec);
        if (ec)
            MonitorLog::instance().warn("dispatch",
                "Failed to create output dir for job " + manifest.job_id + ": " + ec.message());
    }

    // Compute and insert chunks
    auto chunks = computeChunks(manifest.frame_start, manifest.frame_end, manifest.chunk_size);
    if (!m_db->insertChunks(manifest.job_id, chunks))
    {
        m_db->deleteJob(manifest.job_id);
        return {};
    }

    MonitorLog::instance().info("dispatch",
        "Job submitted: " + manifest.job_id + " (" + std::to_string(chunks.size()) + " chunks)");

    return manifest.job_id;
}

// --- Internal ---

void DispatchManager::processSubmissions()
{
    std::queue<SubmitRequest> submissions;
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        std::swap(submissions, m_submitQueue);
    }

    while (!submissions.empty())
    {
        auto& req = submissions.front();
        submitJob(req.manifest, req.priority);
        submissions.pop();
    }
}

void DispatchManager::processReports()
{
    // Drain completions
    std::queue<CompletionReport> completions;
    std::queue<FailureReport> failures;
    std::queue<FrameReport> frameReports;
    std::queue<RevertRequest> reverts;
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        std::swap(completions, m_completionQueue);
        std::swap(failures, m_failureQueue);
        std::swap(frameReports, m_frameQueue);
        std::swap(reverts, m_revertQueue);
    }

    while (!completions.empty())
    {
        auto& r = completions.front();
        auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        m_db->completeChunk(r.job_id, r.frame_start, r.frame_end, nowMs);
        MonitorLog::instance().info("dispatch",
            "Chunk completed: " + r.job_id + " f" + std::to_string(r.frame_start) +
            "-" + std::to_string(r.frame_end) + " by " + r.node_id);
        completions.pop();
    }

    while (!failures.empty())
    {
        auto& r = failures.front();
        // Look up max_retries from the job's manifest
        auto jobOpt = m_db->getJob(r.job_id);
        int maxRetries = 3;
        if (jobOpt.has_value())
        {
            try
            {
                auto manifest = nlohmann::json::parse(jobOpt->manifest_json).get<JobManifest>();
                maxRetries = manifest.max_retries;
            }
            catch (...) {}
        }

        m_db->failChunk(r.job_id, r.frame_start, r.frame_end, maxRetries, r.node_id);

        // Record in machine-level failure tracker
        if (!r.node_id.empty())
        {
            auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            bool wasSuspended = m_failureTracker.isSuspended(r.node_id);
            m_failureTracker.recordFailure(r.node_id, nowMs);
            if (!wasSuspended && m_failureTracker.isSuspended(r.node_id))
            {
                MonitorLog::instance().warn("dispatch",
                    "Node " + r.node_id + " suspended — too many failures in 5 minutes");
            }
        }

        MonitorLog::instance().warn("dispatch",
            "Chunk failed: " + r.job_id + " f" + std::to_string(r.frame_start) +
            "-" + std::to_string(r.frame_end) + " by " + r.node_id + ": " + r.error);
        failures.pop();
    }

    // Drain reverts — no failure tracking, just put chunks back to pending
    while (!reverts.empty())
    {
        auto& r = reverts.front();
        m_db->revertChunkToPending(r.job_id, r.frame_start, r.frame_end);
        MonitorLog::instance().warn("dispatch",
            "Async dispatch failed, reverted: " + r.job_id + " f" +
            std::to_string(r.frame_start) + "-" + std::to_string(r.frame_end));
        reverts.pop();
    }

    // Drain frame completions — batch by job_id for efficiency
    if (!frameReports.empty())
    {
        std::unordered_map<std::string, std::vector<int>> byJob;
        while (!frameReports.empty())
        {
            auto& fr = frameReports.front();
            byJob[fr.job_id].push_back(fr.frame);
            frameReports.pop();
        }
        for (auto& [jobId, frames] : byJob)
        {
            m_db->addCompletedFramesBatch(jobId, frames);
        }
    }
}

void DispatchManager::detectDeadWorkers()
{
    auto peers = m_app->peerManager().getPeerSnapshot();
    for (const auto& p : peers)
    {
        if (p.is_local)
            continue;

        // Existing: fully dead node
        if (!p.is_alive)
        {
            m_db->reassignDeadWorkerChunks(p.node_id);
            continue;
        }

        // New: alive but agent needs manual restart — reclaim chunks
        if (p.agent_health == "needs_attention")
        {
            m_db->reassignDeadWorkerChunks(p.node_id);
            tryRemoteAgentRestart(p);
        }
    }
}

void DispatchManager::checkJobCompletions()
{
    auto jobs = m_db->getAllJobs();
    for (const auto& js : jobs)
    {
        if (js.job.current_state != "active")
            continue;

        std::string finalState = m_db->getJobCompletionState(js.job.job_id);
        if (!finalState.empty())
        {
            m_db->updateJobState(js.job.job_id, finalState);
            MonitorLog::instance().info("dispatch",
                "Job " + finalState + ": " + js.job.job_id);
        }
    }
}

void DispatchManager::assignWork()
{
    // Build set of idle, alive, active workers (including self)
    auto peers = m_app->peerManager().getPeerSnapshot();

    // Include local node info
    auto localInfo = m_app->buildLocalPeerInfo();
    peers.push_back(localInfo);

    // Clear dispatched-node entries once the peer snapshot confirms rendering
    // (heartbeat has propagated), or the node is no longer alive/active.
    for (auto it = m_dispatchedNodes.begin(); it != m_dispatchedNodes.end(); )
    {
        bool confirmed = false;
        for (const auto& p : peers)
        {
            if (p.node_id == *it)
            {
                // Heartbeat caught up — node shows rendering, or it already finished
                if (p.render_state == "rendering" || !p.is_alive)
                    confirmed = true;
                break;
            }
        }
        if (confirmed)
            it = m_dispatchedNodes.erase(it);
        else
            ++it;
    }

    for (const auto& peer : peers)
    {
        // Skip non-alive, stopped, or already rendering nodes
        if (!peer.is_alive)
            continue;
        if (peer.node_state == "stopped")
            continue;
        if (peer.render_state == "rendering")
            continue;

        // Skip nodes we recently dispatched to (heartbeat hasn't caught up yet)
        if (m_dispatchedNodes.count(peer.node_id))
            continue;

        // Skip suspended nodes (machine-level failure tracking)
        if (m_failureTracker.isSuspended(peer.node_id))
            continue;

        // Skip nodes in cooldown after a recent failure
        {
            auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            if (m_failureTracker.isInCooldown(peer.node_id, nowMs))
                continue;
        }

        // Skip nodes with unhealthy agents
        if (peer.agent_health != "ok")
            continue;

        // Skip nodes not yet ready (DCC subprocess still exiting)
        if (!peer.ready_for_work)
            continue;

        // Find next pending chunk this peer is eligible for (respects tags + blacklist)
        auto chunkOpt = m_db->findNextPendingChunkForNode(peer.tags, peer.node_id);
        if (!chunkOpt.has_value())
            continue; // no compatible work for this peer (other peers may still match)

        auto& [chunk, manifestJson] = chunkOpt.value();

        auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        // Assign in DB
        if (!m_db->assignChunk(chunk.id, peer.node_id, nowMs))
            continue;

        // Dispatch to worker
        if (peer.is_local)
        {
            // Self-dispatch via RenderCoordinator
            try
            {
                auto manifest = nlohmann::json::parse(manifestJson).get<JobManifest>();
                ChunkRange cr{chunk.frame_start, chunk.frame_end};
                m_app->renderCoordinator().queueDispatch(manifest, cr);
                m_dispatchedNodes.insert(peer.node_id);
                MonitorLog::instance().info("dispatch",
                    "Self-assigned: " + chunk.job_id + " f" + std::to_string(chunk.frame_start) +
                    "-" + std::to_string(chunk.frame_end));
            }
            catch (const std::exception& e)
            {
                MonitorLog::instance().error("dispatch",
                    std::string("Self-dispatch parse error: ") + e.what());
                // Revert assignment
                m_db->revertChunkToPending(chunk.job_id, chunk.frame_start, chunk.frame_end);
            }
        }
        else
        {
            // HTTP POST to worker (async — off the main thread)
            auto [host, port] = parseEndpoint(peer.endpoint);
            if (host.empty())
            {
                MonitorLog::instance().error("dispatch",
                    "Invalid endpoint for " + peer.node_id + ": " + peer.endpoint);
                // Revert: set back to pending
                m_db->revertChunkToPending(chunk.job_id, chunk.frame_start, chunk.frame_end);
                continue;
            }

            // Mark dispatched optimistically — chunk is already assigned in DB.
            // If the POST fails, the chunk reverts to pending and this entry clears
            // on the next cycle when the node doesn't show as "rendering".
            m_dispatchedNodes.insert(peer.node_id);

            nlohmann::json body = {
                {"manifest", nlohmann::json::parse(manifestJson)},
                {"frame_start", chunk.frame_start},
                {"frame_end", chunk.frame_end},
            };
            std::string bodyStr = body.dump();
            std::string jobId = chunk.job_id;
            int fs = chunk.frame_start;
            int fe = chunk.frame_end;
            std::string nodeId = peer.node_id;

            MonitorLog::instance().info("dispatch",
                "Assigned to " + peer.node_id + ": " + chunk.job_id +
                " f" + std::to_string(chunk.frame_start) +
                "-" + std::to_string(chunk.frame_end));

            std::string secret = m_app->farmSecret();
            std::thread([this, host, port, bodyStr, jobId, fs, fe, nodeId, secret]()
            {
                try
                {
                    httplib::Client cli(host, port);
                    cli.set_connection_timeout(0, 500000); // 500ms
                    cli.set_read_timeout(2);

                    auto res = cli.Post("/api/dispatch/assign", authHeaders(secret), bodyStr, "application/json");
                    if (!res || res->status != 200)
                    {
                        int status = res ? res->status : 0;
                        MonitorLog::instance().warn("dispatch",
                            "Assignment POST failed to " + nodeId +
                            " (status=" + std::to_string(status) + "), queuing revert");
                        RevertRequest rr;
                        rr.job_id = jobId;
                        rr.frame_start = fs;
                        rr.frame_end = fe;
                        queueRevert(std::move(rr));
                    }
                }
                catch (const std::exception& e)
                {
                    MonitorLog::instance().error("dispatch",
                        std::string("HTTP POST error to ") + nodeId + ": " + e.what());
                    RevertRequest rr;
                    rr.job_id = jobId;
                    rr.frame_start = fs;
                    rr.frame_end = fe;
                    queueRevert(std::move(rr));
                }
            }).detach();
        }
    }
}

bool DispatchManager::retryFailedChunks(const std::string& jobId)
{
    if (!m_db || !m_db->isOpen())
        return false;

    return m_db->retryFailedChunks(jobId);
}

std::string DispatchManager::resubmitJob(const std::string& sourceJobId)
{
    if (!m_db || !m_db->isOpen())
        return {};

    auto jobOpt = m_db->getJob(sourceJobId);
    if (!jobOpt.has_value())
        return {};

    try
    {
        auto manifest = nlohmann::json::parse(jobOpt->manifest_json).get<JobManifest>();

        // Generate new job_id: append "-v2", "-v3", etc.
        std::string baseSlug = manifest.job_id;
        // Strip existing -vN suffix
        auto vpos = baseSlug.rfind("-v");
        if (vpos != std::string::npos)
        {
            bool allDigits = true;
            for (size_t i = vpos + 2; i < baseSlug.size(); ++i)
            {
                if (!std::isdigit(static_cast<unsigned char>(baseSlug[i])))
                {
                    allDigits = false;
                    break;
                }
            }
            if (allDigits && vpos + 2 < baseSlug.size())
                baseSlug = baseSlug.substr(0, vpos);
        }

        // Find next available suffix
        std::string newJobId;
        for (int suffix = 2; suffix < 1000; ++suffix)
        {
            newJobId = baseSlug + "-v" + std::to_string(suffix);
            if (!m_db->getJob(newJobId).has_value())
                break;
        }

        // Update manifest with new job_id and fresh timestamp
        manifest.job_id = newJobId;
        manifest.submitted_at_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        // Submit as new job (fresh chunks, zero retry counts, empty failed_on)
        return submitJob(manifest, jobOpt->priority);
    }
    catch (const std::exception& e)
    {
        MonitorLog::instance().error("dispatch",
            std::string("resubmitJob failed: ") + e.what());
        return {};
    }
}

std::string DispatchManager::resubmitJob(const std::string& sourceJobId,
                                          int frameStart, int frameEnd, int chunkSize)
{
    if (!m_db || !m_db->isOpen())
    {
        MonitorLog::instance().error("dispatch", "resubmitJob (override): DB not open");
        return {};
    }

    auto jobOpt = m_db->getJob(sourceJobId);
    if (!jobOpt.has_value())
    {
        MonitorLog::instance().error("dispatch",
            "resubmitJob (override): source job not found: " + sourceJobId);
        return {};
    }

    try
    {
        auto manifest = nlohmann::json::parse(jobOpt->manifest_json).get<JobManifest>();

        // Override frame range and chunk size
        manifest.frame_start = frameStart;
        manifest.frame_end = frameEnd;
        manifest.chunk_size = chunkSize;

        // Generate new job_id: append "-v2", "-v3", etc.
        std::string baseSlug = manifest.job_id;
        // Strip existing -vN suffix
        auto vpos = baseSlug.rfind("-v");
        if (vpos != std::string::npos)
        {
            bool allDigits = true;
            for (size_t i = vpos + 2; i < baseSlug.size(); ++i)
            {
                if (!std::isdigit(static_cast<unsigned char>(baseSlug[i])))
                {
                    allDigits = false;
                    break;
                }
            }
            if (allDigits && vpos + 2 < baseSlug.size())
                baseSlug = baseSlug.substr(0, vpos);
        }

        // Find next available suffix
        std::string newJobId;
        for (int suffix = 2; suffix < 1000; ++suffix)
        {
            newJobId = baseSlug + "-v" + std::to_string(suffix);
            if (!m_db->getJob(newJobId).has_value())
                break;
        }

        // Update manifest with new job_id and fresh timestamp
        manifest.job_id = newJobId;
        manifest.submitted_at_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        // Submit as new job (fresh chunks, zero retry counts, empty failed_on)
        return submitJob(manifest, jobOpt->priority);
    }
    catch (const std::exception& e)
    {
        MonitorLog::instance().error("dispatch",
            std::string("resubmitJob (override) failed: ") + e.what());
        return {};
    }
}

std::string DispatchManager::submitJobWithChunks(const JobManifest& manifest, int priority,
                                                  const std::vector<ChunkRange>& chunks)
{
    if (!m_db || !m_db->isOpen())
        return {};

    // Insert job row
    JobRow row;
    row.job_id = manifest.job_id;
    row.manifest_json = nlohmann::json(manifest).dump();
    row.current_state = "active";
    row.priority = priority;
    row.submitted_at_ms = manifest.submitted_at_ms;

    if (!m_db->insertJob(row))
        return {};

    // Create output directory once (leader-only, at submission time)
    if (manifest.output_dir.has_value() && !manifest.output_dir.value().empty())
    {
        std::error_code ec;
        std::filesystem::create_directories(manifest.output_dir.value(), ec);
        if (ec)
            MonitorLog::instance().warn("dispatch",
                "Failed to create output dir for job " + manifest.job_id + ": " + ec.message());
    }

    // Insert caller-provided chunks
    if (!m_db->insertChunks(manifest.job_id, chunks))
    {
        m_db->deleteJob(manifest.job_id);
        return {};
    }

    MonitorLog::instance().info("dispatch",
        "Job submitted: " + manifest.job_id + " (" + std::to_string(chunks.size()) + " chunks)");

    return manifest.job_id;
}

std::string DispatchManager::resubmitIncomplete(const std::string& sourceJobId)
{
    if (!m_db || !m_db->isOpen())
        return {};

    auto jobOpt = m_db->getJob(sourceJobId);
    if (!jobOpt.has_value())
        return {};

    try
    {
        auto manifest = nlohmann::json::parse(jobOpt->manifest_json).get<JobManifest>();

        // Read all chunks from source job
        auto sourceChunks = m_db->getChunksForJob(sourceJobId);
        if (sourceChunks.empty())
            return {};

        // Separate into completed and all ranges
        std::vector<ChunkRange> allRanges;
        std::vector<ChunkRange> completedRanges;
        bool hasIncomplete = false;

        for (const auto& c : sourceChunks)
        {
            ChunkRange cr{c.frame_start, c.frame_end};
            allRanges.push_back(cr);
            if (c.state == "completed")
                completedRanges.push_back(cr);
            else
                hasIncomplete = true;
        }

        if (!hasIncomplete)
            return {};  // nothing to resubmit

        // Generate new job_id: append "-v2", "-v3", etc. (same pattern as resubmitJob)
        std::string baseSlug = manifest.job_id;
        auto vpos = baseSlug.rfind("-v");
        if (vpos != std::string::npos)
        {
            bool allDigits = true;
            for (size_t i = vpos + 2; i < baseSlug.size(); ++i)
            {
                if (!std::isdigit(static_cast<unsigned char>(baseSlug[i])))
                {
                    allDigits = false;
                    break;
                }
            }
            if (allDigits && vpos + 2 < baseSlug.size())
                baseSlug = baseSlug.substr(0, vpos);
        }

        std::string newJobId;
        for (int suffix = 2; suffix < 1000; ++suffix)
        {
            newJobId = baseSlug + "-v" + std::to_string(suffix);
            if (!m_db->getJob(newJobId).has_value())
                break;
        }

        // Update manifest with new job_id and fresh timestamp
        manifest.job_id = newJobId;
        manifest.submitted_at_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        // Submit with all chunk ranges (preserves full frame grid)
        std::string result = submitJobWithChunks(manifest, jobOpt->priority, allRanges);
        if (result.empty())
            return {};

        // Mark previously-completed chunks as completed in the new job
        m_db->markChunksCompleted(newJobId, completedRanges);

        MonitorLog::instance().info("dispatch",
            "Resubmitted incomplete: " + sourceJobId + " -> " + newJobId +
            " (" + std::to_string(allRanges.size() - completedRanges.size()) + " incomplete chunks)");

        return newJobId;
    }
    catch (const std::exception& e)
    {
        MonitorLog::instance().error("dispatch",
            std::string("resubmitIncomplete failed: ") + e.what());
        return {};
    }
}

void DispatchManager::tryRemoteAgentRestart(const PeerInfo& peer)
{
    auto now = std::chrono::steady_clock::now();
    auto it = m_lastAgentRestartAttempt.find(peer.node_id);
    if (it != m_lastAgentRestartAttempt.end())
    {
        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - it->second).count();
        if (elapsedMs < AGENT_RESTART_COOLDOWN_MS)
            return;
    }
    m_lastAgentRestartAttempt[peer.node_id] = now;

    auto [host, port] = parseEndpoint(peer.endpoint);
    if (host.empty())
        return;

    MonitorLog::instance().info("dispatch",
        "Attempting remote agent restart on " + peer.node_id);

    std::string secret = m_app->farmSecret();
    std::thread([host, port, nodeId = peer.node_id, secret]()
    {
        try
        {
            httplib::Client cli(host, port);
            cli.set_connection_timeout(2);
            cli.set_read_timeout(5);
            auto res = cli.Post("/api/agent/restart", authHeaders(secret), "", "application/json");
            if (res && res->status == 200)
                MonitorLog::instance().info("dispatch",
                    "Remote agent restart succeeded on " + nodeId);
            else
                MonitorLog::instance().warn("dispatch",
                    "Remote agent restart failed on " + nodeId);
        }
        catch (...) {}
    }).detach();
}

void DispatchManager::doSnapshot()
{
    if (!m_app || !m_db || !m_db->isOpen())
        return;

    // Skip if previous snapshot is still running
    if (m_snapshotInProgress.exchange(true))
        return;

    // Move the entire snapshot (SQLite backup + file copy) off the main thread.
    auto localTmp = m_app->farmPath().parent_path() / "snapshot_tmp.db";
    auto snapshotPath = m_app->farmPath() / "state" / "snapshot.db";
    auto* db = m_db;
    auto* flag = &m_snapshotInProgress;

    std::thread([db, localTmp, snapshotPath, flag]()
    {
        if (db->snapshotTo(localTmp))
        {
            std::error_code ec;
            std::filesystem::copy_file(localTmp, snapshotPath,
                std::filesystem::copy_options::overwrite_existing, ec);
            std::filesystem::remove(localTmp, ec);
            if (!ec)
                MonitorLog::instance().info("dispatch", "DB snapshot written");
            else
                MonitorLog::instance().warn("dispatch", "Snapshot copy failed: " + ec.message());
        }
        flag->store(false);
    }).detach();
}

} // namespace MR

#include "core/http_server.h"
#include "core/peer_info.h"
#include "core/monitor_log.h"
#include "core/platform.h"
#include "core/path_mapping.h"
#include "monitor/monitor_app.h"
#include "monitor/dispatch_manager.h"
#include "monitor/database_manager.h"

#include <nlohmann/json.hpp>
#include <thread>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>

namespace MR {

void HttpServer::init(MonitorApp* app)
{
    m_app = app;
}

bool HttpServer::requireLeader(httplib::Response& res)
{
    if (!m_app->isLeader())
    {
        nlohmann::json body = {{"error", "not_leader"}};
        auto peers = m_app->peerManager().getPeerSnapshot();
        for (const auto& p : peers)
        {
            if (p.is_leader)
            {
                body["leader_endpoint"] = p.endpoint;
                break;
            }
        }
        res.status = 503;
        res.set_content(body.dump(), "application/json");
        return false;
    }
    return true;
}

void HttpServer::setupRoutes()
{
    // GET /api/status -- this node's PeerInfo as JSON
    m_server.Get("/api/status", [this](const httplib::Request&, httplib::Response& res)
    {
        if (!m_app)
        {
            res.status = 503;
            return;
        }

        PeerInfo info = m_app->buildLocalPeerInfo();
        nlohmann::json j = info;
        res.set_content(j.dump(), "application/json");
    });

    // GET /api/peers -- list of known peers (from PeerManager snapshot)
    m_server.Get("/api/peers", [this](const httplib::Request&, httplib::Response& res)
    {
        if (!m_app)
        {
            res.status = 503;
            return;
        }

        auto peers = m_app->peerManager().getPeerSnapshot();
        nlohmann::json j = peers;
        res.set_content(j.dump(), "application/json");
    });

    // --- Remote node control (every node) ---

    // POST /api/node/stop -- remotely stop rendering on this node
    m_server.Post("/api/node/stop", [this](const httplib::Request&, httplib::Response& res)
    {
        if (!m_app) { res.status = 503; return; }
        m_app->setNodeState(NodeState::Stopped);
        MonitorLog::instance().info("farm", "Remotely stopped by peer");
        res.set_content(R"({"status":"ok"})", "application/json");
    });

    // POST /api/node/start -- remotely resume rendering on this node
    m_server.Post("/api/node/start", [this](const httplib::Request&, httplib::Response& res)
    {
        if (!m_app) { res.status = 503; return; }
        m_app->setNodeState(NodeState::Active);
        MonitorLog::instance().info("farm", "Remotely started by peer");
        res.set_content(R"({"status":"ok"})", "application/json");
    });

    // POST /api/node/restart -- remotely restart the entire monitor app via sidecar
    m_server.Post("/api/node/restart", [this](const httplib::Request&, httplib::Response& res)
    {
        if (!m_app) { res.status = 503; return; }
        MonitorLog::instance().info("farm", "Remote app restart requested");
        if (m_app->launchRestartSidecar())
            res.set_content(R"({"status":"ok"})", "application/json");
        else
        {
            res.status = 500;
            res.set_content(R"({"error":"restart_failed"})", "application/json");
        }
    });

    // --- Remote agent control (every node) ---

    // POST /api/agent/restart -- remotely restart agent on this node
    m_server.Post("/api/agent/restart", [this](const httplib::Request&, httplib::Response& res)
    {
        if (!m_app) { res.status = 503; return; }
        MonitorLog::instance().info("agent", "Remote agent restart requested");
        m_app->agentSupervisor().shutdownAgent();
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        if (m_app->agentSupervisor().spawnAgent())
        {
            m_app->agentSupervisor().resetHealth();
            res.set_content(R"({"status":"ok"})", "application/json");
        }
        else
        {
            res.status = 500;
            res.set_content(R"({"error":"agent_spawn_failed"})", "application/json");
        }
    });

    // --- Worker endpoint (every node) ---

    // POST /api/dispatch/assign -- receives assignment from leader
    m_server.Post("/api/dispatch/assign", [this](const httplib::Request& req, httplib::Response& res)
    {
        if (!m_app)
        {
            res.status = 503;
            return;
        }

        // If this node is currently rendering, reject
        if (m_app->renderCoordinator().isRendering())
        {
            res.status = 409;
            res.set_content(R"({"error":"busy"})", "application/json");
            return;
        }

        // If stopped, reject
        if (m_app->nodeState() == NodeState::Stopped)
        {
            res.status = 409;
            res.set_content(R"({"error":"stopped"})", "application/json");
            return;
        }

        try
        {
            auto body = nlohmann::json::parse(req.body);
            auto manifest = body.at("manifest").get<JobManifest>();
            int frameStart = body.at("frame_start").get<int>();
            int frameEnd = body.at("frame_end").get<int>();

            ChunkRange chunk{frameStart, frameEnd};
            m_app->renderCoordinator().queueDispatch(manifest, chunk);

            res.set_content(R"({"status":"ok"})", "application/json");
        }
        catch (const std::exception& e)
        {
            res.status = 400;
            nlohmann::json err = {{"error", e.what()}};
            res.set_content(err.dump(), "application/json");
        }
    });

    // --- Leader-only endpoints ---

    // POST /api/jobs -- submit a new job
    m_server.Post("/api/jobs", [this](const httplib::Request& req, httplib::Response& res)
    {
        if (!requireLeader(res)) return;

        try
        {
            auto body = nlohmann::json::parse(req.body);
            auto manifest = body.at("manifest").get<JobManifest>();
            int priority = body.value("priority", 50);

            SubmitRequest sr;
            sr.manifest = std::move(manifest);
            sr.priority = priority;
            m_app->dispatchManager().queueSubmission(std::move(sr));

            res.set_content(R"({"status":"ok"})", "application/json");
        }
        catch (const std::exception& e)
        {
            res.status = 400;
            nlohmann::json err = {{"error", e.what()}};
            res.set_content(err.dump(), "application/json");
        }
    });

    // GET /api/jobs -- list all jobs with progress
    m_server.Get("/api/jobs", [this](const httplib::Request&, httplib::Response& res)
    {
        if (!requireLeader(res)) return;

        auto json = m_app->getCachedJobsJson();
        res.set_content(json, "application/json");
    });

    // GET /api/jobs/:id -- single job detail + chunks
    m_server.Get(R"(/api/jobs/([^/]+))", [this](const httplib::Request& req, httplib::Response& res)
    {
        if (!requireLeader(res)) return;

        std::string jobId = req.matches[1];
        auto json = m_app->getCachedJobDetailJson(jobId);
        if (json.empty())
        {
            res.status = 404;
            res.set_content(R"({"error":"not_found"})", "application/json");
            return;
        }
        res.set_content(json, "application/json");
    });

    // POST /api/jobs/:id/pause
    m_server.Post(R"(/api/jobs/([^/]+)/pause)", [this](const httplib::Request& req, httplib::Response& res)
    {
        if (!requireLeader(res)) return;
        std::string jobId = req.matches[1];
        m_app->pauseJob(jobId);
        res.set_content(R"({"status":"ok"})", "application/json");
    });

    // POST /api/jobs/:id/resume
    m_server.Post(R"(/api/jobs/([^/]+)/resume)", [this](const httplib::Request& req, httplib::Response& res)
    {
        if (!requireLeader(res)) return;
        std::string jobId = req.matches[1];
        m_app->resumeJob(jobId);
        res.set_content(R"({"status":"ok"})", "application/json");
    });

    // POST /api/jobs/:id/cancel
    m_server.Post(R"(/api/jobs/([^/]+)/cancel)", [this](const httplib::Request& req, httplib::Response& res)
    {
        if (!requireLeader(res)) return;
        std::string jobId = req.matches[1];
        m_app->cancelJob(jobId);
        res.set_content(R"({"status":"ok"})", "application/json");
    });

    // POST /api/jobs/:id/archive
    m_server.Post(R"(/api/jobs/([^/]+)/archive)", [this](const httplib::Request& req, httplib::Response& res)
    {
        if (!requireLeader(res)) return;
        std::string jobId = req.matches[1];
        m_app->archiveJob(jobId);
        res.set_content(R"({"status":"ok"})", "application/json");
    });

    // DELETE /api/jobs/:id
    m_server.Delete(R"(/api/jobs/([^/]+))", [this](const httplib::Request& req, httplib::Response& res)
    {
        if (!requireLeader(res)) return;
        std::string jobId = req.matches[1];
        m_app->deleteJob(jobId);
        res.set_content(R"({"status":"ok"})", "application/json");
    });

    // POST /api/jobs/:id/retry-failed -- retry only failed chunks
    m_server.Post(R"(/api/jobs/([^/]+)/retry-failed)", [this](const httplib::Request& req, httplib::Response& res)
    {
        if (!requireLeader(res)) return;
        std::string jobId = req.matches[1];
        m_app->retryFailedChunks(jobId);
        res.set_content(R"({"status":"ok"})", "application/json");
    });

    // POST /api/jobs/:id/resubmit -- create new job from existing manifest
    // Optional JSON body with frame_start, frame_end, chunk_size for chunk resubmit
    m_server.Post(R"(/api/jobs/([^/]+)/resubmit)", [this](const httplib::Request& req, httplib::Response& res)
    {
        if (!requireLeader(res)) return;
        std::string jobId = req.matches[1];

        std::string newId;
        if (!req.body.empty())
        {
            try
            {
                auto body = nlohmann::json::parse(req.body);
                if (body.value("incomplete_only", false))
                {
                    newId = m_app->resubmitIncomplete(jobId);
                }
                else if (body.contains("frame_start"))
                {
                    int frameStart = body.at("frame_start").get<int>();
                    int frameEnd = body.at("frame_end").get<int>();
                    int chunkSize = body.at("chunk_size").get<int>();
                    newId = m_app->resubmitChunkAsJob(jobId, frameStart, frameEnd, chunkSize);
                }
                else
                {
                    newId = m_app->resubmitJob(jobId);
                }
            }
            catch (const std::exception& e)
            {
                res.status = 400;
                nlohmann::json err = {{"error", e.what()}};
                res.set_content(err.dump(), "application/json");
                return;
            }
        }
        else
        {
            newId = m_app->resubmitJob(jobId);
        }

        if (newId.empty())
        {
            res.status = 404;
            res.set_content(R"({"error":"resubmit_failed"})", "application/json");
            return;
        }
        nlohmann::json resp = {{"status", "ok"}, {"job_id", newId}};
        res.set_content(resp.dump(), "application/json");
    });

    // POST /api/chunks/reassign -- reassign a chunk to pending or to a specific node
    m_server.Post("/api/chunks/reassign", [this](const httplib::Request& req, httplib::Response& res)
    {
        if (!requireLeader(res)) return;

        try
        {
            auto body = nlohmann::json::parse(req.body);
            int64_t chunkId = body.at("chunk_id").get<int64_t>();
            std::string targetNode = body.value("target_node", "");

            m_app->reassignChunk(chunkId, targetNode);
            res.set_content(R"({"status":"ok"})", "application/json");
        }
        catch (const std::exception& e)
        {
            res.status = 400;
            nlohmann::json err = {{"error", e.what()}};
            res.set_content(err.dump(), "application/json");
        }
    });

    // POST /api/nodes/:id/unsuspend -- clear failure tracking for a node
    m_server.Post(R"(/api/nodes/([^/]+)/unsuspend)", [this](const httplib::Request& req, httplib::Response& res)
    {
        if (!requireLeader(res)) return;
        std::string nodeId = req.matches[1];
        m_app->unsuspendNode(nodeId);
        res.set_content(R"({"status":"ok"})", "application/json");
    });

    // GET /api/nodes/suspended -- list of suspended nodes with failure counts
    m_server.Get("/api/nodes/suspended", [this](const httplib::Request&, httplib::Response& res)
    {
        if (!requireLeader(res)) return;
        auto suspended = m_app->dispatchManager().failureTracker().getSuspended();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& [nodeId, record] : suspended)
        {
            arr.push_back({
                {"node_id", nodeId},
                {"failure_count", record.failure_count},
                {"first_failure_ms", record.first_failure_ms},
                {"last_failure_ms", record.last_failure_ms},
            });
        }
        res.set_content(arr.dump(), "application/json");
    });

    // POST /api/dispatch/frame-complete -- worker reports per-frame completions
    m_server.Post("/api/dispatch/frame-complete", [this](const httplib::Request& req, httplib::Response& res)
    {
        if (!requireLeader(res)) return;

        try
        {
            auto body = nlohmann::json::parse(req.body);
            std::string nodeId = body.value("node_id", "");
            std::string jobId = body.at("job_id").get<std::string>();
            auto frames = body.at("frames").get<std::vector<int>>();

            for (int frame : frames)
            {
                FrameReport fr;
                fr.node_id = nodeId;
                fr.job_id = jobId;
                fr.frame = frame;
                m_app->dispatchManager().queueFrameCompletion(std::move(fr));
            }

            res.set_content(R"({"status":"ok"})", "application/json");
        }
        catch (const std::exception& e)
        {
            res.status = 400;
            nlohmann::json err = {{"error", e.what()}};
            res.set_content(err.dump(), "application/json");
        }
    });

    // POST /api/dispatch/complete -- worker reports chunk completion
    m_server.Post("/api/dispatch/complete", [this](const httplib::Request& req, httplib::Response& res)
    {
        if (!requireLeader(res)) return;

        try
        {
            auto body = nlohmann::json::parse(req.body);
            CompletionReport report;
            report.node_id = body.value("node_id", "");
            report.job_id = body.at("job_id").get<std::string>();
            report.frame_start = body.at("frame_start").get<int>();
            report.frame_end = body.at("frame_end").get<int>();
            report.elapsed_ms = body.value("elapsed_ms", int64_t(0));
            report.exit_code = body.value("exit_code", 0);

            m_app->dispatchManager().queueCompletion(std::move(report));
            res.set_content(R"({"status":"ok"})", "application/json");
        }
        catch (const std::exception& e)
        {
            res.status = 400;
            nlohmann::json err = {{"error", e.what()}};
            res.set_content(err.dump(), "application/json");
        }
    });

    // POST /api/dispatch/failed -- worker reports chunk failure
    m_server.Post("/api/dispatch/failed", [this](const httplib::Request& req, httplib::Response& res)
    {
        if (!requireLeader(res)) return;

        try
        {
            auto body = nlohmann::json::parse(req.body);
            FailureReport report;
            report.node_id = body.value("node_id", "");
            report.job_id = body.at("job_id").get<std::string>();
            report.frame_start = body.at("frame_start").get<int>();
            report.frame_end = body.at("frame_end").get<int>();
            report.error = body.value("error", std::string("Unknown"));

            m_app->dispatchManager().queueFailure(std::move(report));
            res.set_content(R"({"status":"ok"})", "application/json");
        }
        catch (const std::exception& e)
        {
            res.status = 400;
            nlohmann::json err = {{"error", e.what()}};
            res.set_content(err.dump(), "application/json");
        }
    });

    // ---------------------------------------------------------------
    // Tauri UI support endpoints
    // ---------------------------------------------------------------

    // GET /api/jobs/:id/task-output -- list chunk log files for a job
    m_server.Get(R"(/api/jobs/([^/]+)/task-output)", [this](const httplib::Request& req, httplib::Response& res)
    {
        if (!m_app || !m_app->isFarmRunning()) { res.status = 503; return; }

        std::string jobId = req.matches[1];
        auto stdoutDir = m_app->farmPath() / "jobs" / jobId / "stdout";

        namespace fs = std::filesystem;
        std::error_code ec;
        if (!fs::is_directory(stdoutDir, ec))
        {
            res.set_content("[]", "application/json");
            return;
        }

        nlohmann::json arr = nlohmann::json::array();
        for (const auto& nodeEntry : fs::directory_iterator(stdoutDir, ec))
        {
            if (!nodeEntry.is_directory()) continue;
            std::string nodeId = nodeEntry.path().filename().string();

            for (const auto& fileEntry : fs::directory_iterator(nodeEntry.path(), ec))
            {
                if (!fileEntry.is_regular_file()) continue;
                std::string fname = fileEntry.path().filename().string();
                if (fname.size() < 5 || fname.substr(fname.size() - 4) != ".log")
                    continue;

                std::string stem = fname.substr(0, fname.size() - 4);
                auto underPos = stem.rfind('_');
                if (underPos == std::string::npos) continue;

                std::string rangeStr = stem.substr(0, underPos);
                int64_t timestampMs = 0;
                try { timestampMs = std::stoll(stem.substr(underPos + 1)); }
                catch (...) { continue; }

                arr.push_back({
                    {"node_id", nodeId},
                    {"range", rangeStr},
                    {"timestamp_ms", timestampMs},
                    {"path", fileEntry.path().string()}
                });
            }
        }

        res.set_content(arr.dump(), "application/json");
    });

    // GET /api/jobs/:id/task-output/:node/:filename -- read a chunk log file
    m_server.Get(R"(/api/jobs/([^/]+)/task-output/([^/]+)/([^/]+))",
        [this](const httplib::Request& req, httplib::Response& res)
    {
        if (!m_app || !m_app->isFarmRunning()) { res.status = 503; return; }

        std::string jobId = req.matches[1];
        std::string nodeId = req.matches[2];
        std::string filename = req.matches[3];

        auto logPath = m_app->farmPath() / "jobs" / jobId / "stdout" / nodeId / filename;

        std::ifstream ifs(logPath, std::ios::in);
        if (!ifs.is_open())
        {
            res.status = 404;
            res.set_content(R"({"error":"not_found"})", "application/json");
            return;
        }

        std::string content((std::istreambuf_iterator<char>(ifs)),
                             std::istreambuf_iterator<char>());
        res.set_content(content, "text/plain");
    });

    // GET /api/nodes/:id/log -- read a remote node's monitor log
    m_server.Get(R"(/api/nodes/([^/]+)/log)", [this](const httplib::Request& req, httplib::Response& res)
    {
        if (!m_app || !m_app->isFarmRunning()) { res.status = 503; return; }

        std::string nodeId = req.matches[1];
        int maxLines = 500;
        if (req.has_param("max_lines"))
        {
            try { maxLines = std::stoi(req.get_param_value("max_lines")); }
            catch (...) {}
        }

        auto lines = MonitorLog::readNodeLog(m_app->farmPath(), nodeId, maxLines);
        nlohmann::json j = lines;
        res.set_content(j.dump(), "application/json");
    });

    // POST /api/farm/scan-cleanup -- scan for cleanup items
    m_server.Post("/api/farm/scan-cleanup", [this](const httplib::Request&, httplib::Response& res)
    {
        if (!m_app || !m_app->isFarmRunning()) { res.status = 503; return; }

        namespace fs = std::filesystem;
        std::error_code ec;
        nlohmann::json result;

        // Sections 1 & 2: Finished/Archived jobs (leader only)
        nlohmann::json finishedJobs = nlohmann::json::array();
        nlohmann::json archivedJobs = nlohmann::json::array();
        std::set<std::string> dbJobIds;

        if (m_app->isLeader() && m_app->databaseManager().isOpen())
        {
            auto allJobs = m_app->databaseManager().getAllJobs();
            for (const auto& s : allJobs)
            {
                dbJobIds.insert(s.job.job_id);
                if (s.job.current_state == "completed" || s.job.current_state == "cancelled")
                {
                    finishedJobs.push_back({
                        {"id", s.job.job_id},
                        {"label", s.job.job_id},
                        {"detail", s.job.current_state + " | " + std::to_string(s.progress.total) + " chunks"}
                    });
                }
                else if (s.job.current_state == "archived")
                {
                    archivedJobs.push_back({
                        {"id", s.job.job_id},
                        {"label", s.job.job_id},
                        {"detail", "archived"}
                    });
                }
            }
        }
        result["finished_jobs"] = finishedJobs;
        result["archived_jobs"] = archivedJobs;

        // Section 3: Orphaned directories
        nlohmann::json orphanedDirs = nlohmann::json::array();
        {
            auto jobsDir = m_app->farmPath() / "jobs";
            if (fs::is_directory(jobsDir, ec))
            {
                // If not leader, use cached job IDs
                if (dbJobIds.empty())
                {
                    for (const auto& j : m_app->cachedJobs())
                        dbJobIds.insert(j.manifest.job_id);
                }

                for (const auto& entry : fs::directory_iterator(jobsDir, ec))
                {
                    if (!entry.is_directory()) continue;
                    std::string dirName = entry.path().filename().string();
                    if (dbJobIds.find(dirName) == dbJobIds.end())
                    {
                        orphanedDirs.push_back({
                            {"id", entry.path().string()},
                            {"label", dirName},
                            {"detail", "no matching job"}
                        });
                    }
                }
            }
        }
        result["orphaned_dirs"] = orphanedDirs;

        // Section 4: Stale peers
        nlohmann::json stalePeers = nlohmann::json::array();
        {
            auto peers = m_app->peerManager().getPeerSnapshot();
            for (const auto& p : peers)
            {
                if (!p.is_alive && !p.is_local)
                {
                    stalePeers.push_back({
                        {"id", p.node_id},
                        {"label", p.hostname + " (" + p.node_id.substr(0, 8) + ")"},
                        {"detail", "last seen: " + std::to_string(p.last_seen_ms)}
                    });
                }
            }
        }
        result["stale_peers"] = stalePeers;

        // Sections 5 & 6: Staging directories
        nlohmann::json staleStagingDirs = nlohmann::json::array();
        nlohmann::json failedStagingCopies = nlohmann::json::array();
        {
            auto stagingRoot = getAppDataDir() / "staging";
            if (fs::is_directory(stagingRoot, ec))
            {
                for (const auto& jobEntry : fs::directory_iterator(stagingRoot, ec))
                {
                    if (!jobEntry.is_directory(ec)) continue;
                    std::string jobName = jobEntry.path().filename().string();

                    int fileCount = 0;
                    uintmax_t totalSize = 0;
                    for (const auto& f : fs::recursive_directory_iterator(jobEntry.path(), ec))
                    {
                        if (f.is_regular_file(ec))
                        {
                            fileCount++;
                            totalSize += f.file_size(ec);
                        }
                    }

                    if (fileCount > 0)
                    {
                        std::string sizeStr;
                        if (totalSize < 1024 * 1024)
                            sizeStr = std::to_string(totalSize / 1024) + " KB";
                        else if (totalSize < 1024ULL * 1024 * 1024)
                            sizeStr = std::to_string(totalSize / (1024 * 1024)) + " MB";
                        else
                            sizeStr = std::to_string(totalSize / (1024ULL * 1024 * 1024)) + " GB";

                        failedStagingCopies.push_back({
                            {"id", jobEntry.path().string()},
                            {"label", jobName},
                            {"detail", std::to_string(fileCount) + " files (" + sizeStr + ")"}
                        });
                    }
                    else
                    {
                        staleStagingDirs.push_back({
                            {"id", jobEntry.path().string()},
                            {"label", jobName},
                            {"detail", "empty"}
                        });
                    }
                }
            }
        }
        result["stale_staging_dirs"] = staleStagingDirs;
        result["failed_staging_copies"] = failedStagingCopies;
        result["is_leader"] = m_app->isLeader();

        res.set_content(result.dump(), "application/json");
    });

    // POST /api/farm/cleanup -- execute cleanup actions on selected items
    m_server.Post("/api/farm/cleanup", [this](const httplib::Request& req, httplib::Response& res)
    {
        if (!m_app || !m_app->isFarmRunning()) { res.status = 503; return; }

        namespace fs = std::filesystem;
        std::error_code ec;

        try
        {
            auto body = nlohmann::json::parse(req.body);
            std::string action = body.at("action").get<std::string>();
            auto ids = body.at("ids").get<std::vector<std::string>>();
            int count = 0;

            if (action == "archive")
            {
                for (const auto& id : ids)
                {
                    m_app->archiveJob(id);
                    count++;
                }
            }
            else if (action == "delete_jobs")
            {
                for (const auto& id : ids)
                {
                    m_app->deleteJob(id);
                    count++;
                }
            }
            else if (action == "delete_dirs")
            {
                for (const auto& id : ids)
                {
                    fs::remove_all(fs::path(id), ec);
                    if (!ec) count++;
                    else MonitorLog::instance().warn("farm", "Cleanup: failed to remove " + id + ": " + ec.message());
                }
            }
            else if (action == "remove_peers")
            {
                for (const auto& id : ids)
                {
                    auto nodeDir = m_app->farmPath() / "nodes" / id;
                    fs::remove_all(nodeDir, ec);
                    if (!ec) count++;
                    else MonitorLog::instance().warn("farm", "Cleanup: failed to remove peer " + id + ": " + ec.message());
                }
            }
            else
            {
                res.status = 400;
                res.set_content(R"({"error":"unknown_action"})", "application/json");
                return;
            }

            nlohmann::json resp = {{"status", "ok"}, {"count", count}};
            res.set_content(resp.dump(), "application/json");
        }
        catch (const std::exception& e)
        {
            res.status = 400;
            nlohmann::json err = {{"error", e.what()}};
            res.set_content(err.dump(), "application/json");
        }
    });

    // GET /api/config -- return full sidecar config
    m_server.Get("/api/config", [this](const httplib::Request&, httplib::Response& res)
    {
        if (!m_app) { res.status = 503; return; }
        nlohmann::json j = m_app->config();
        // Also include node identity info
        j["node_id"] = m_app->identity().nodeId();
        j["hostname"] = m_app->identity().systemInfo().hostname;
        j["cpu_cores"] = m_app->identity().systemInfo().cpuCores;
        j["ram_mb"] = m_app->identity().systemInfo().ramMB;
        j["gpu_name"] = m_app->identity().systemInfo().gpuName;
        j["farm_running"] = m_app->isFarmRunning();
        j["farm_path"] = m_app->farmPath().string();
        j["farm_error"] = m_app->farmError();
        j["is_leader"] = m_app->isLeader();
        res.set_content(j.dump(), "application/json");
    });

    // POST /api/config -- update sidecar config (partial update)
    m_server.Post("/api/config", [this](const httplib::Request& req, httplib::Response& res)
    {
        if (!m_app) { res.status = 503; return; }

        try
        {
            auto body = nlohmann::json::parse(req.body);
            auto& cfg = m_app->config();
            bool needsRestart = false;

            if (body.contains("sync_root"))
            {
                std::string newRoot = body["sync_root"].get<std::string>();
                if (newRoot != cfg.sync_root) { cfg.sync_root = newRoot; needsRestart = true; }
            }
            if (body.contains("http_port"))
            {
                uint16_t p = body["http_port"].get<uint16_t>();
                if (p != cfg.http_port) { cfg.http_port = p; needsRestart = true; }
            }
            if (body.contains("ip_override"))
                cfg.ip_override = body["ip_override"].get<std::string>();
            if (body.contains("udp_enabled"))
            {
                bool v = body["udp_enabled"].get<bool>();
                if (v != cfg.udp_enabled) { cfg.udp_enabled = v; needsRestart = true; }
            }
            if (body.contains("udp_port"))
            {
                uint16_t p = body["udp_port"].get<uint16_t>();
                if (p != cfg.udp_port) { cfg.udp_port = p; needsRestart = true; }
            }
            if (body.contains("priority"))
                cfg.priority = body["priority"].get<int>();
            if (body.contains("tags"))
                cfg.tags = body["tags"].get<std::vector<std::string>>();
            if (body.contains("staging_enabled"))
                cfg.staging_enabled = body["staging_enabled"].get<bool>();
            if (body.contains("rndr_dual_mode"))
                cfg.rndr_dual_mode = body["rndr_dual_mode"].get<bool>();
            if (body.contains("show_notifications"))
                cfg.show_notifications = body["show_notifications"].get<bool>();
            if (body.contains("path_mappings"))
                cfg.path_mappings = body["path_mappings"].get<std::vector<PathMapping>>();

            m_app->saveConfig();
            MonitorLog::instance().info("config", "Config updated via API");

            // Flag for deferred farm restart on the main thread.
            // Cannot restart here — we're on the HTTP handler thread,
            // and stopFarm() joins threads including the HTTP server.
            if (needsRestart)
                m_app->requestFarmRestart();

            nlohmann::json resp = {{"status", "ok"}, {"restarted", needsRestart}};
            res.set_content(resp.dump(), "application/json");
        }
        catch (const std::exception& e)
        {
            res.status = 400;
            nlohmann::json err = {{"error", e.what()}};
            res.set_content(err.dump(), "application/json");
        }
    });

    // POST /api/config/path-mappings -- accept path mappings from Tauri UI
    m_server.Post("/api/config/path-mappings", [this](const httplib::Request& req, httplib::Response& res)
    {
        if (!m_app) { res.status = 503; return; }

        try
        {
            auto body = nlohmann::json::parse(req.body);
            auto mappings = body.get<std::vector<PathMapping>>();
            m_app->config().path_mappings = std::move(mappings);
            m_app->saveConfig();
            MonitorLog::instance().info("config",
                "Updated " + std::to_string(m_app->config().path_mappings.size()) + " path mapping(s)");
            res.set_content(R"({"status":"ok"})", "application/json");
        }
        catch (const std::exception& e)
        {
            res.status = 400;
            nlohmann::json err = {{"error", e.what()}};
            res.set_content(err.dump(), "application/json");
        }
    });
}

bool HttpServer::start(const std::string& bindAddress, uint16_t port)
{
    if (m_running.load())
        return true;

    setupRoutes();

    // Auth middleware: require Bearer token on all /api/ routes except GET /api/status
    if (!m_apiSecret.empty())
    {
        m_server.set_pre_routing_handler([this](const httplib::Request& req, httplib::Response& res) -> httplib::Server::HandlerResponse
        {
            // Skip auth for GET /api/status (used by peer discovery)
            // and /api/config* (used by local Tauri UI)
            if (req.method == "GET" && req.path == "/api/status")
                return httplib::Server::HandlerResponse::Unhandled;
            if (req.path.rfind("/api/config", 0) == 0)
                return httplib::Server::HandlerResponse::Unhandled;

            // Only protect /api/ routes
            if (req.path.rfind("/api/", 0) != 0)
                return httplib::Server::HandlerResponse::Unhandled;

            auto it = req.headers.find("Authorization");
            if (it == req.headers.end() || it->second != "Bearer " + m_apiSecret)
            {
                res.status = 401;
                res.set_content(R"({"error":"unauthorized"})", "application/json");
                return httplib::Server::HandlerResponse::Handled;
            }

            return httplib::Server::HandlerResponse::Unhandled;
        });
    }

    m_port = port;

    // Try to bind before launching thread
    if (!m_server.bind_to_port(bindAddress, port))
    {
        MonitorLog::instance().error("http", "Failed to bind HTTP server to " +
            bindAddress + ":" + std::to_string(port));
        return false;
    }

    m_running.store(true);
    m_thread = std::thread([this]()
    {
        MonitorLog::instance().info("http", "HTTP server listening on port " +
            std::to_string(m_port));
        m_server.listen_after_bind();
        m_running.store(false);
    });

    return true;
}

void HttpServer::stop()
{
    if (!m_running.load())
        return;

    m_server.stop();
    if (m_thread.joinable())
        m_thread.join();

    m_running.store(false);
    MonitorLog::instance().info("http", "HTTP server stopped");
}

} // namespace MR

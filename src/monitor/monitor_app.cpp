#include "monitor/monitor_app.h"
#include "monitor/farm_init.h"
#include "core/platform.h"
#include "core/monitor_log.h"
#include "core/net_utils.h"

#include <nlohmann/json.hpp>
#include <httplib.h>
#include <fstream>
#include <iostream>
#include <chrono>
#include <unordered_map>

#ifdef _WIN32
#include <windows.h>
#endif

namespace MR {

bool MonitorApp::init()
{
    // Load node identity
    m_appDataDir = getAppDataDir();
    m_identity.loadOrGenerate(m_appDataDir);
    m_identity.querySystemInfo();

    // Load config
    m_configPath = m_appDataDir / "config.json";
    loadConfig();

    // Cleanup leftover restart.bat from older versions
    {
        std::error_code ec;
        std::filesystem::remove(m_appDataDir / "restart.bat", ec);
    }

    // Font scale is applied by the Qt UI layer via AppBridge (Phase 2+).
    // The backend only stores the value in m_config.font_scale.

    // Restore persisted node state
    if (m_config.node_stopped)
        setNodeState(NodeState::Stopped);

    // Initialize HTTP server (routes set up before farm starts)
    m_httpServer.init(this);

    // Initialize agent supervisor
    m_agentSupervisor.start(m_identity.nodeId());

    // Detect RNDR binary
    m_rndrSupervisor.detectBinary();

    // Set up agent message handler
    m_agentSupervisor.setMessageHandler([this](const std::string& type, const nlohmann::json& msg)
    {
        m_renderCoordinator.handleAgentMessage(type, msg);
    });

    // Start background HTTP worker
    startHttpWorker();

    // Start UI IPC server (for Tauri push events)
    m_uiIpc.start(m_identity.nodeId());
    m_uiIpc.setCommandHandler([this](const std::string& json)
    {
        handleUiCommand(json);
    });

    // Push log entries to UI in real time
    MonitorLog::instance().setCallback([this](const MonitorLog::Entry& e)
    {
        if (!m_uiIpc.isConnected()) return;
        nlohmann::json j;
        j["type"] = "log_entry";
        j["timestamp_ms"] = e.timestamp_ms;
        j["level"] = e.level;
        j["category"] = e.category;
        j["message"] = e.message;
        m_uiIpc.push(j.dump());
    });

    // (Qt UI layer initializes separately in main_qt.cpp via AppBridge;
    // the backend here is UI-agnostic.)

    // Auto-start farm if sync_root is configured
    if (!m_config.sync_root.empty() &&
        std::filesystem::is_directory(m_config.sync_root))
    {
        startFarm();
    }

    return true;
}

void MonitorApp::update()
{
    // Poll UDP messages (fast path — every frame)
    if (m_udpNotify.isRunning())
    {
        handleUdpMessages();

        // Send UDP heartbeat every ~3s
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - m_lastUdpHeartbeat).count();
        if (elapsed >= 3000)
        {
            sendUdpHeartbeat();
            m_lastUdpHeartbeat = now;
        }
    }

    // Process agent messages
    m_agentSupervisor.processMessages();

    // Render coordinator update
    m_renderCoordinator.update(m_agentSupervisor);

    // Leader transition detection
    if (isLeader() && !m_wasLeader)
    {
        onBecomeLeader();
        if (m_uiIpc.isConnected())
        {
            nlohmann::json j;
            j["type"] = "leader_change";
            j["is_leader"] = true;
            j["leader_id"] = m_identity.nodeId();
            j["leader_endpoint"] = getLeaderEndpoint();
            m_uiIpc.push(j.dump());
        }
    }
    if (!isLeader() && m_wasLeader)
    {
        onLoseLeadership();
        if (m_uiIpc.isConnected())
        {
            nlohmann::json j;
            j["type"] = "leader_change";
            j["is_leader"] = false;
            j["leader_id"] = "";
            j["leader_endpoint"] = getLeaderEndpoint();
            m_uiIpc.push(j.dump());
        }
    }
    m_wasLeader = isLeader();

    // If leader: run dispatch cycle (gated on background DB init)
    if (isLeader() && m_leaderDbReady.load() && m_databaseManager.isOpen())
        m_dispatchManager.update();

    // Refresh cached jobs + templates periodically
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - m_lastJobCacheRefresh).count();
    if (elapsed >= 2000)
    {
        refreshCachedJobs();
        m_cachedTemplates = m_templateManager.getTemplateSnapshot();
        m_lastJobCacheRefresh = now;

        // Push state snapshot to connected UI client
        if (m_uiIpc.isConnected())
            pushStateSnapshot();
    }

    // Poll local submission dropbox
    m_submissionWatcher.poll();

    // Note: completion/frame reports are flushed by the background HTTP worker thread.

    // RNDR dual mode
    m_rndrSupervisor.update(m_renderCoordinator.isRendering(), m_config.rndr_dual_mode);

    // Update render state on PeerManager
    if (m_renderCoordinator.isRendering())
    {
        m_peerManager.setRenderState("rendering",
            m_renderCoordinator.currentJobId(),
            m_renderCoordinator.currentChunkLabel());

        // Push render progress to UI (~1s throttle)
        if (m_uiIpc.isConnected())
        {
            auto progElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - m_lastProgressPush).count();
            if (progElapsed >= 1000)
            {
                nlohmann::json j;
                j["type"] = "render_progress";
                j["job_id"] = m_renderCoordinator.currentJobId();
                j["chunk_label"] = m_renderCoordinator.currentChunkLabel();
                j["progress_pct"] = m_renderCoordinator.currentProgress();
                m_uiIpc.push(j.dump());
                m_lastProgressPush = now;
            }
        }
    }
    else
    {
        m_peerManager.setRenderState("idle");
    }

    // Deferred farm restart (triggered by POST /api/config)
    if (m_farmRestartRequested.exchange(false))
    {
        MonitorLog::instance().info("config", "Restarting farm (deferred from config update)");
        if (m_farmRunning) stopFarm();
        if (!m_config.sync_root.empty()) startFarm();
    }

    // Check for filesystem restart signal
    if (m_peerManager.consumeRestartSignal())
    {
        MonitorLog::instance().info("farm", "Restart signal detected via filesystem");
        launchRestartSidecar();
    }

    // Deferred restart — wait for sidecar to start before shutting down
    if (m_restartPending)
    {
        auto restartElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - m_restartLaunchedAt).count();
        if (restartElapsed >= 3000)
        {
            m_restartPending = false;
            beginForceExit();
        }
    }

    // Process exit
    if (m_exitRequested && !m_shouldExit)
    {
        if (!m_renderCoordinator.isRendering())
        {
            m_shouldExit = true;
        }
    }
}


void MonitorApp::shutdown()
{
    MonitorLog::instance().setCallback(nullptr);
    m_uiIpc.stop();
    stopHttpWorker();
    stopFarm();

    m_renderCoordinator.abortCurrentRender("shutdown");
    m_agentSupervisor.stop();
    m_rndrSupervisor.shutdown();

    saveConfig();
}

// ---------------------------------------------------------------------------
// UI IPC push events
// ---------------------------------------------------------------------------

void MonitorApp::pushStateSnapshot()
{
    nlohmann::json j;
    j["type"] = "state_snapshot";

    // Local node
    j["local_node"] = buildLocalPeerInfo();

    // Peers
    j["peers"] = m_peerManager.getPeerSnapshot();

    // Jobs — reuse the cached JSON string to avoid re-serializing
    {
        std::lock_guard<std::mutex> lock(m_cachedJsonMutex);
        j["jobs"] = nlohmann::json::parse(m_cachedJobsJson, nullptr, false);
    }

    // Farm state
    j["farm_running"] = m_farmRunning;
    j["farm_path"] = m_farmPath.string();
    j["farm_error"] = m_farmError;
    j["is_leader"] = isLeader();
    j["leader_endpoint"] = getLeaderEndpoint();

    // Render state
    j["render_state"] = m_renderCoordinator.isRendering() ? "rendering" : "idle";
    if (m_renderCoordinator.isRendering())
    {
        j["active_job"] = m_renderCoordinator.currentJobId();
        j["active_chunk"] = m_renderCoordinator.currentChunkLabel();
        j["render_progress"] = m_renderCoordinator.currentProgress();
    }

    // Agent health
    j["agent_health"] = m_agentSupervisor.agentHealth();
    j["agent_connected"] = m_agentSupervisor.isAgentConnected();

    // Tray state (for macOS Swift companion or external consumers)
    j["tray_icon"] = trayIconName();
    j["tray_tooltip"] = trayTooltip();
    j["tray_status"] = trayStatusText();

    // Node state
    j["node_state"] = (m_nodeState == NodeState::Active) ? "active" : "stopped";

    // RNDR state
    j["rndr_available"] = m_rndrSupervisor.isBinaryAvailable();
    j["rndr_status"] = m_rndrSupervisor.statusText();
    static const char* rndrStateNames[] = {"inactive", "running", "render_active", "cooldown"};
    j["rndr_state"] = rndrStateNames[static_cast<int>(m_rndrSupervisor.state())];

    m_uiIpc.push(j.dump());
}

void MonitorApp::handleUiCommand(const std::string& json)
{
    try
    {
        auto j = nlohmann::json::parse(json);
        std::string type = j.value("type", "");

        if (type == "ping")
        {
            nlohmann::json pong;
            pong["type"] = "pong";
            m_uiIpc.push(pong.dump());
        }
        else if (type == "get_state")
        {
            pushStateSnapshot();
        }
        else if (type == "get_logs")
        {
            // Send buffered log entries to newly connected UI client
            auto entries = MonitorLog::instance().getEntries();
            for (const auto& e : entries)
            {
                nlohmann::json lj;
                lj["type"] = "log_entry";
                lj["timestamp_ms"] = e.timestamp_ms;
                lj["level"] = e.level;
                lj["category"] = e.category;
                lj["message"] = e.message;
                m_uiIpc.push(lj.dump());
            }
        }
        else if (type == "get_chunks")
        {
            std::string jobId = j.value("job_id", "");
            if (!jobId.empty())
            {
                auto chunks = getChunksForJob(jobId);
                nlohmann::json resp;
                resp["type"] = "chunks";
                resp["job_id"] = jobId;
                nlohmann::json arr = nlohmann::json::array();
                for (const auto& c : chunks)
                {
                    arr.push_back({
                        {"chunk_id", c.id},
                        {"frame_start", c.frame_start},
                        {"frame_end", c.frame_end},
                        {"state", c.state},
                        {"assigned_to", c.assigned_to},
                        {"retries", c.retry_count},
                        {"completed_frames", c.completed_frames},
                        {"failed_on", c.failed_on}
                    });
                }
                resp["chunks"] = std::move(arr);
                m_uiIpc.push(resp.dump());
            }
        }
    }
    catch (const std::exception& e)
    {
        MonitorLog::instance().warn("ui-ipc", std::string("Bad command: ") + e.what());
    }
}

void MonitorApp::loadConfig()
{
    std::error_code ec;
    if (!std::filesystem::exists(m_configPath, ec))
    {
        std::cout << "[MonitorApp] No config found, using defaults" << std::endl;
        return;
    }

    try
    {
        std::ifstream ifs(m_configPath);
        nlohmann::json j = nlohmann::json::parse(ifs);
        m_config = j.get<Config>();
        std::cout << "[MonitorApp] Config loaded" << std::endl;
    }
    catch (const std::exception& e)
    {
        std::cerr << "[MonitorApp] Failed to load config: " << e.what() << std::endl;
    }
}

void MonitorApp::saveConfig()
{
    try
    {
        nlohmann::json j = m_config;
        std::ofstream ofs(m_configPath);
        ofs << j.dump(2);
        std::cout << "[MonitorApp] Config saved" << std::endl;
    }
    catch (const std::exception& e)
    {
        std::cerr << "[MonitorApp] Failed to save config: " << e.what() << std::endl;
    }
}

bool MonitorApp::startFarm()
{
    if (m_farmRunning)
        return true;

    m_farmPath = std::filesystem::path(m_config.sync_root) /
        ("MinRender-v" + std::to_string(PROTOCOL_VERSION));
    m_farmError.clear();

    // Check if sync root exists
    std::error_code ec;
    if (!std::filesystem::is_directory(m_config.sync_root, ec))
    {
        m_farmError = "Sync root does not exist: " + m_config.sync_root;
        return false;
    }

    // Create farm directory structure if needed
    std::filesystem::create_directories(m_farmPath / "templates" / "examples", ec);
    std::filesystem::create_directories(m_farmPath / "jobs", ec);
    std::filesystem::create_directories(m_farmPath / "nodes", ec);
    std::filesystem::create_directories(m_farmPath / "state", ec);

    // Farm init — copy example templates + plugins on first run or version change
    auto initResult = FarmInit::init(m_farmPath, m_identity.nodeId());
    if (!initResult.success)
        MonitorLog::instance().warn("farm", "Farm init: " + initResult.error);

    // Load API secret from farm.json
    {
        auto farmJsonPath = m_farmPath / "farm.json";
        try
        {
            std::ifstream ifs(farmJsonPath);
            nlohmann::json fj = nlohmann::json::parse(ifs);
            if (fj.contains("api_secret") && fj["api_secret"].is_string())
                m_farmSecret = fj["api_secret"].get<std::string>();
        }
        catch (...) {}
    }
    m_httpServer.setApiSecret(m_farmSecret);

    // Start file logging
    MonitorLog::instance().startFileLogging(m_farmPath, m_identity.nodeId());
    MonitorLog::instance().info("farm", "Farm started at " + m_farmPath.string());

    // Apply staging setting
    m_renderCoordinator.setStagingEnabled(m_config.staging_enabled);

    // Initialize render coordinator with completion callbacks
    m_renderCoordinator.init(m_farmPath, m_identity.nodeId(), getOS(),
        [this](const std::string& jobId, const ChunkRange& chunk, const std::string& state)
        {
            reportCompletion(jobId, chunk, state);
        },
        [this](const std::string& jobId, int frame)
        {
            reportFrameCompletion(jobId, frame);
        },
        &m_agentSupervisor);

    // Initialize dispatch manager (will be active only when leader)
    m_dispatchManager.init(this, &m_databaseManager);

    // Start HTTP server
    std::string localIp = m_config.ip_override.empty()
        ? getLocalIpAddress()
        : m_config.ip_override;
    uint16_t httpPort = m_config.http_port;

    if (!m_httpServer.start("0.0.0.0", httpPort))
    {
        m_farmError = "Failed to bind HTTP server on port " + std::to_string(httpPort);
        MonitorLog::instance().error("farm", m_farmError);
    }

    // Start peer manager
    std::string localEndpoint = localIp + ":" + std::to_string(httpPort);
    m_peerManager.start(m_farmPath, m_identity.nodeId(), localEndpoint,
                        m_config.priority, m_config.tags);
    MonitorLog::instance().info("farm", "Local endpoint: " + localEndpoint);

    // Start UDP multicast (after PeerManager)
    if (m_config.udp_enabled)
    {
        if (m_udpNotify.start(m_identity.nodeId(), m_config.udp_port))
        {
            MonitorLog::instance().info("farm",
                "UDP multicast active on port " + std::to_string(m_config.udp_port));
            // Send immediate announcement
            sendUdpHeartbeat();
            m_lastUdpHeartbeat = std::chrono::steady_clock::now();
        }
        else
        {
            MonitorLog::instance().warn("farm",
                "UDP multicast failed to start — running HTTP-only");
        }
    }

    // Start template manager
    m_templateManager.start(m_farmPath);

    // Initialize local submission watcher
    m_submissionWatcher.init(this, m_appDataDir);

    m_farmRunning = true;
    m_wasLeader = false;

    return true;
}

void MonitorApp::stopFarm()
{
    if (!m_farmRunning)
        return;

    // Wait for any in-progress leader DB init
    if (m_leaderThread.joinable())
        m_leaderThread.join();
    m_leaderDbReady.store(false);

    // Send UDP goodbye before stopping
    if (m_udpNotify.isRunning())
    {
        nlohmann::json bye = {
            {"t", "bye"},
            {"from", m_identity.nodeId()},
            {"n", m_identity.nodeId()},
        };
        m_udpNotify.send(bye);
        m_udpNotify.stop();
    }

    // Close database before stopping managers
    m_databaseManager.close();

    m_peerManager.stop();
    m_httpServer.stop();
    m_templateManager.stop();
    MonitorLog::instance().info("farm", "Farm stopped");
    MonitorLog::instance().stopFileLogging();

    m_farmRunning = false;
    m_wasLeader = false;
}

// --- Leader transitions ---

void MonitorApp::onBecomeLeader()
{
    MonitorLog::instance().info("farm", "This node is now leader — initializing DB...");
    m_leaderDbReady.store(false);

    // Join any previous transition thread
    if (m_leaderThread.joinable())
        m_leaderThread.join();

    // DB restore/open on background thread to avoid blocking UI
    auto snapshotPath = m_farmPath / "state" / "snapshot.db";
    auto localDbPath = m_appDataDir / "minrender.db";

    m_leaderThread = std::thread([this, snapshotPath, localDbPath]()
    {
        std::error_code ec;
        if (std::filesystem::exists(snapshotPath, ec))
        {
            MonitorLog::instance().info("farm", "Restoring DB from snapshot...");
            if (m_databaseManager.restoreFrom(snapshotPath, localDbPath))
            {
                MonitorLog::instance().info("farm", "DB restored from snapshot");
                m_leaderDbReady.store(true);
                return;
            }
            MonitorLog::instance().warn("farm", "Snapshot restore failed, opening fresh DB");
        }

        if (m_databaseManager.open(localDbPath))
            m_leaderDbReady.store(true);
        else
            MonitorLog::instance().error("farm", "Failed to open database!");
    });
}

void MonitorApp::onLoseLeadership()
{
    MonitorLog::instance().info("farm", "No longer leader");

    // Wait for any in-progress DB init to finish before closing
    if (m_leaderThread.joinable())
        m_leaderThread.join();

    m_leaderDbReady.store(false);
    m_databaseManager.close();
}

// --- Completion reporting ---

void MonitorApp::reportCompletion(const std::string& jobId, const ChunkRange& chunk,
                                   const std::string& state)
{
    MonitorLog::instance().info("render",
        "Chunk " + state + ": " + jobId + " " + chunk.rangeStr());

    if (state == "abandoned")
        return; // no report needed

    if (isLeader() && m_databaseManager.isOpen())
    {
        // Direct DB update
        auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        if (state == "completed")
        {
            m_databaseManager.completeChunk(jobId, chunk.frame_start, chunk.frame_end, nowMs);
        }
        else if (state == "failed")
        {
            auto jobOpt = m_databaseManager.getJob(jobId);
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
            m_databaseManager.failChunk(jobId, chunk.frame_start, chunk.frame_end,
                maxRetries, m_identity.nodeId());
        }
    }
    else
    {
        // Buffer for HTTP delivery to leader (flushed by worker thread)
        PendingReport report;
        report.jobId = jobId;
        report.frameStart = chunk.frame_start;
        report.frameEnd = chunk.frame_end;
        report.state = state;
        std::lock_guard<std::mutex> lock(m_reportMutex);
        m_pendingReports.push_back(std::move(report));
    }
}

// --- Frame completion reporting ---

void MonitorApp::reportFrameCompletion(const std::string& jobId, int frame)
{
    if (isLeader() && m_databaseManager.isOpen())
    {
        // Direct DB write — no queue, no 2s dispatch throttle.
        // We're already on the main thread (agent message handler).
        m_databaseManager.addCompletedFrames(jobId, frame);
    }
    else
    {
        std::lock_guard<std::mutex> lock(m_reportMutex);
        m_pendingFrameReports.push_back({jobId, frame});
    }
}

// --- Chunk data access (leader: DB, worker: HTTP) ---

std::vector<ChunkRow> MonitorApp::getChunksForJob(const std::string& jobId)
{
    if (isLeader() && m_leaderDbReady.load() && m_databaseManager.isOpen())
    {
        return m_databaseManager.getChunksForJob(jobId);
    }

    // Worker: fetch from leader via HTTP (skip if cooldown active)
    if (std::chrono::steady_clock::now() < m_leaderContactCooldown)
        return {};

    std::string leaderEp = getLeaderEndpoint();
    if (leaderEp.empty())
        return {};

    auto [host, port] = parseEndpoint(leaderEp);
    if (host.empty())
        return {};

    try
    {
        httplib::Client cli(host, port);
        cli.set_connection_timeout(0, 500000); // 500ms
        cli.set_read_timeout(1);

        auto res = cli.Get("/api/jobs/" + jobId, authHeaders(m_farmSecret));
        if (!res || res->status != 200)
        {
            if (!res)
                m_leaderContactCooldown = std::chrono::steady_clock::now() +
                                         std::chrono::seconds(5);
            return {};
        }

        auto body = nlohmann::json::parse(res->body);
        if (!body.contains("chunks") || !body["chunks"].is_array())
            return {};

        std::vector<ChunkRow> result;
        for (const auto& cj : body["chunks"])
        {
            ChunkRow row;
            row.id = cj.value("id", int64_t(0));
            row.frame_start = cj.value("frame_start", 0);
            row.frame_end = cj.value("frame_end", 0);
            row.state = cj.value("state", "pending");
            row.assigned_to = cj.value("assigned_to", "");
            row.assigned_at_ms = cj.value("assigned_at_ms", int64_t(0));
            row.completed_at_ms = cj.value("completed_at_ms", int64_t(0));
            row.retry_count = cj.value("retry_count", 0);
            if (cj.contains("completed_frames") && cj["completed_frames"].is_array())
                row.completed_frames = cj["completed_frames"].get<std::vector<int>>();
            if (cj.contains("failed_on") && cj["failed_on"].is_array())
                row.failed_on = cj["failed_on"].get<std::vector<std::string>>();
            result.push_back(std::move(row));
        }
        return result;
    }
    catch (...)
    {
        m_leaderContactCooldown = std::chrono::steady_clock::now() +
                                 std::chrono::seconds(5);
    }
    return {};
}

// --- Cached job data ---

void MonitorApp::refreshCachedJobs()
{
    if (isLeader() && m_leaderDbReady.load() && m_databaseManager.isOpen())
    {
        // Build from DB
        auto summaries = m_databaseManager.getAllJobs();

        std::vector<JobInfo> jobs;
        nlohmann::json jsonArr = nlohmann::json::array();

        for (auto& s : summaries)
        {
            // Skip archived jobs — they're hidden from the main job list
            if (s.job.current_state == "archived")
                continue;

            JobInfo info;
            try
            {
                info.manifest = nlohmann::json::parse(s.job.manifest_json).get<JobManifest>();
            }
            catch (...) { continue; }

            info.current_state = s.job.current_state;
            info.current_priority = s.job.priority;
            info.total_chunks = s.progress.total;
            info.completed_chunks = s.progress.completed;
            info.failed_chunks = s.progress.failed;
            info.rendering_chunks = s.progress.rendering;

            // Build JSON for HTTP cache BEFORE moving info
            nlohmann::json jj = {
                {"job_id", s.job.job_id},
                {"template_id", info.manifest.template_id},
                {"current_state", s.job.current_state},
                {"priority", s.job.priority},
                {"submitted_at_ms", s.job.submitted_at_ms},
                {"submitted_by", info.manifest.submitted_by},
                {"frame_start", info.manifest.frame_start},
                {"frame_end", info.manifest.frame_end},
                {"chunk_size", info.manifest.chunk_size},
                {"total_chunks", s.progress.total},
                {"completed_chunks", s.progress.completed},
                {"failed_chunks", s.progress.failed},
                {"rendering_chunks", s.progress.rendering},
                {"pending_chunks", s.progress.pending},
            };
            if (info.manifest.output_dir.has_value())
                jj["output_dir"] = info.manifest.output_dir.value();
            jsonArr.push_back(std::move(jj));

            jobs.push_back(std::move(info));
        }

        m_cachedJobs = std::move(jobs);

        {
            std::lock_guard<std::mutex> lock(m_cachedJsonMutex);
            m_cachedJobsJson = jsonArr.dump();
        }
    }
    else if (m_farmRunning)
    {
        // Worker: fetch from leader via HTTP (skip if cooldown active)
        if (std::chrono::steady_clock::now() >= m_leaderContactCooldown)
        {
            std::string leaderEp = getLeaderEndpoint();
            if (!leaderEp.empty())
            {
                auto [host, port] = parseEndpoint(leaderEp);
                if (!host.empty())
                {
                    try
                    {
                        httplib::Client cli(host, port);
                        cli.set_connection_timeout(0, 500000); // 500ms
                        cli.set_read_timeout(1);

                        auto res = cli.Get("/api/jobs", authHeaders(m_farmSecret));
                        if (res && res->status == 200)
                        {
                            auto arr = nlohmann::json::parse(res->body);
                            std::vector<JobInfo> jobs;

                            for (const auto& jj : arr)
                            {
                                JobInfo info;
                                info.manifest.job_id = jj.value("job_id", "");
                                info.manifest.template_id = jj.value("template_id", "");
                                info.manifest.submitted_by = jj.value("submitted_by", "");
                                info.manifest.submitted_at_ms = jj.value("submitted_at_ms", int64_t(0));
                                info.manifest.frame_start = jj.value("frame_start", 1);
                                info.manifest.frame_end = jj.value("frame_end", 250);
                                info.manifest.chunk_size = jj.value("chunk_size", 1);
                                info.current_state = jj.value("current_state", "active");
                                info.current_priority = jj.value("priority", 50);
                                info.total_chunks = jj.value("total_chunks", 0);
                                info.completed_chunks = jj.value("completed_chunks", 0);
                                info.failed_chunks = jj.value("failed_chunks", 0);
                                info.rendering_chunks = jj.value("rendering_chunks", 0);
                                if (jj.contains("output_dir") && jj["output_dir"].is_string())
                                    info.manifest.output_dir = jj["output_dir"].get<std::string>();
                                jobs.push_back(std::move(info));
                            }

                            m_cachedJobs = std::move(jobs);

                            {
                                std::lock_guard<std::mutex> lock(m_cachedJsonMutex);
                                m_cachedJobsJson = res->body;
                            }
                        }
                        else
                        {
                            m_leaderContactCooldown = std::chrono::steady_clock::now() +
                                                     std::chrono::seconds(5);
                        }
                    }
                    catch (...)
                    {
                        m_leaderContactCooldown = std::chrono::steady_clock::now() +
                                                 std::chrono::seconds(5);
                    }
                }
            }
        }
    }
}

std::string MonitorApp::getCachedJobsJson() const
{
    std::lock_guard<std::mutex> lock(m_cachedJsonMutex);
    return m_cachedJobsJson;
}

std::string MonitorApp::getCachedJobDetailJson(const std::string& jobId) const
{
    if (!m_leaderDbReady.load() || !m_databaseManager.isOpen())
        return {};

    auto jobOpt = const_cast<DatabaseManager&>(m_databaseManager).getJob(jobId);
    if (!jobOpt.has_value())
        return {};

    auto chunks = const_cast<DatabaseManager&>(m_databaseManager).getChunksForJob(jobId);

    nlohmann::json result = {
        {"job_id", jobOpt->job_id},
        {"current_state", jobOpt->current_state},
        {"priority", jobOpt->priority},
        {"submitted_at_ms", jobOpt->submitted_at_ms},
        {"manifest", nlohmann::json::parse(jobOpt->manifest_json)},
    };

    nlohmann::json chunkArr = nlohmann::json::array();
    for (const auto& c : chunks)
    {
        chunkArr.push_back({
            {"id", c.id},
            {"frame_start", c.frame_start},
            {"frame_end", c.frame_end},
            {"state", c.state},
            {"assigned_to", c.assigned_to},
            {"assigned_at_ms", c.assigned_at_ms},
            {"completed_at_ms", c.completed_at_ms},
            {"retry_count", c.retry_count},
            {"completed_frames", c.completed_frames},
            {"failed_on", c.failed_on},
        });
    }
    result["chunks"] = chunkArr;

    return result.dump();
}

// --- Job controls ---

void MonitorApp::pauseJob(const std::string& jobId)
{
    if (isLeader() && m_databaseManager.isOpen())
    {
        m_databaseManager.updateJobState(jobId, "paused");
        MonitorLog::instance().info("job", "Paused job: " + jobId);
    }
    else
    {
        postToLeaderAsync("/api/jobs/" + jobId + "/pause", "");
    }
}

void MonitorApp::resumeJob(const std::string& jobId)
{
    if (isLeader() && m_databaseManager.isOpen())
    {
        m_databaseManager.updateJobState(jobId, "active");
        MonitorLog::instance().info("job", "Resumed job: " + jobId);
    }
    else
    {
        postToLeaderAsync("/api/jobs/" + jobId + "/resume", "");
    }
}

void MonitorApp::cancelJob(const std::string& jobId)
{
    if (isLeader() && m_databaseManager.isOpen())
    {
        m_databaseManager.updateJobState(jobId, "cancelled");
        MonitorLog::instance().info("job", "Cancelled job: " + jobId);
    }
    else
    {
        postToLeaderAsync("/api/jobs/" + jobId + "/cancel", "");
    }

    // Abort local render if it's for this job
    if (m_renderCoordinator.isRendering() && m_renderCoordinator.currentJobId() == jobId)
    {
        m_renderCoordinator.abortCurrentRender("job cancelled");
    }
    m_renderCoordinator.purgeJob(jobId);
}

void MonitorApp::requeueJob(const std::string& jobId)
{
    if (isLeader() && m_databaseManager.isOpen())
    {
        m_databaseManager.resetAllChunks(jobId);
        m_databaseManager.updateJobState(jobId, "active");
        MonitorLog::instance().info("job", "Requeued job: " + jobId);
    }
    else
    {
        // Log the leader's reply so a 404 (leader on old build without
        // this endpoint) or auth failure shows up locally instead of
        // silently looking like "button does nothing".
        postToLeaderAsync("/api/jobs/" + jobId + "/requeue", "",
            [jobId](bool ok, const std::string& resp) {
                if (ok)
                    MonitorLog::instance().info("job", "Requeued job: " + jobId);
                else
                {
                    std::string msg = "Requeue failed for " + jobId;
                    if (!resp.empty()) msg += " — leader responded: " + resp;
                    else               msg += " — no response (leader unreachable or missing endpoint)";
                    MonitorLog::instance().warn("job", msg);
                }
            });
    }
}

void MonitorApp::deleteJob(const std::string& jobId)
{
    // Cancel first (abort any active render)
    cancelJob(jobId);

    if (isLeader() && m_databaseManager.isOpen())
    {
        m_databaseManager.deleteJob(jobId);
        MonitorLog::instance().info("job", "Deleted job: " + jobId);
    }
    else
    {
        postToLeaderAsync("/api/jobs/" + jobId, "", nullptr, "DELETE");
    }

    // Clean up shared FS job directory
    if (m_farmRunning)
    {
        auto jobDir = m_farmPath / "jobs" / jobId;
        std::error_code ec;
        std::filesystem::remove_all(jobDir, ec);
        if (ec)
            MonitorLog::instance().warn("job", "Failed to remove job dir: " + ec.message());
    }

    if (m_selectedJobId == jobId)
        m_selectedJobId.clear();
}

void MonitorApp::archiveJob(const std::string& jobId)
{
    if (isLeader() && m_databaseManager.isOpen())
    {
        m_databaseManager.updateJobState(jobId, "archived");
        MonitorLog::instance().info("job", "Archived job: " + jobId);
    }
    else
    {
        postToLeaderAsync("/api/jobs/" + jobId + "/archive", "");
    }

    if (m_selectedJobId == jobId)
        m_selectedJobId.clear();
}

void MonitorApp::retryFailedChunks(const std::string& jobId)
{
    if (isLeader() && m_databaseManager.isOpen())
    {
        m_dispatchManager.retryFailedChunks(jobId);
        MonitorLog::instance().info("job", "Retrying failed chunks: " + jobId);
    }
    else
    {
        postToLeaderAsync("/api/jobs/" + jobId + "/retry-failed", "");
    }
}

void MonitorApp::reassignChunk(int64_t chunkId, const std::string& targetNodeId)
{
    if (isLeader() && m_databaseManager.isOpen())
    {
        m_databaseManager.reassignChunk(chunkId, targetNodeId);
        MonitorLog::instance().info("job", "Reassigned chunk " + std::to_string(chunkId) +
            (targetNodeId.empty() ? " to pending" : " to " + targetNodeId));
    }
    else
    {
        nlohmann::json body = {{"chunk_id", chunkId}};
        if (!targetNodeId.empty())
            body["target_node"] = targetNodeId;
        postToLeaderAsync("/api/chunks/reassign", body.dump());
    }
}

std::string MonitorApp::resubmitJob(const std::string& jobId)
{
    if (isLeader() && m_databaseManager.isOpen())
    {
        auto newId = m_dispatchManager.resubmitJob(jobId);
        if (!newId.empty())
        {
            MonitorLog::instance().info("job", "Resubmitted job: " + jobId + " -> " + newId);
            selectJob(newId);
        }
        return newId;
    }
    else
    {
        postToLeaderAsync("/api/jobs/" + jobId + "/resubmit", "");
    }
    return {};
}

std::string MonitorApp::resubmitChunkAsJob(const std::string& jobId,
                                            int frameStart, int frameEnd, int chunkSize)
{
    if (isLeader() && m_databaseManager.isOpen())
    {
        auto newId = m_dispatchManager.resubmitJob(jobId, frameStart, frameEnd, chunkSize);
        if (!newId.empty())
        {
            MonitorLog::instance().info("job", "Resubmitted chunk as job: "
                + jobId + " [" + std::to_string(frameStart) + "-"
                + std::to_string(frameEnd) + "] -> " + newId);
            selectJob(newId);
        }
        return newId;
    }
    else
    {
        nlohmann::json body = {
            {"frame_start", frameStart},
            {"frame_end", frameEnd},
            {"chunk_size", chunkSize},
        };
        MonitorLog::instance().info("job", "Sending resubmit-chunk to leader: " + jobId);
        postToLeaderAsync("/api/jobs/" + jobId + "/resubmit", body.dump(),
            [jobId](bool success, const std::string& response) {
                if (success)
                    MonitorLog::instance().info("job", "Resubmit-chunk succeeded for " + jobId
                        + ": " + response);
                else
                    MonitorLog::instance().warn("job", "Resubmit-chunk failed for " + jobId
                        + (response.empty() ? "" : ": " + response));
            });
    }
    return {};
}

std::string MonitorApp::resubmitIncomplete(const std::string& jobId)
{
    if (isLeader() && m_databaseManager.isOpen())
    {
        auto newId = m_dispatchManager.resubmitIncomplete(jobId);
        if (!newId.empty())
        {
            MonitorLog::instance().info("job",
                "Resubmitted incomplete: " + jobId + " -> " + newId);
            selectJob(newId);
        }
        return newId;
    }
    else
    {
        nlohmann::json body = {{"incomplete_only", true}};
        postToLeaderAsync("/api/jobs/" + jobId + "/resubmit", body.dump());
    }
    return {};
}

void MonitorApp::unsuspendNode(const std::string& nodeId)
{
    if (isLeader())
    {
        m_dispatchManager.failureTracker().clearNode(nodeId);
        MonitorLog::instance().info("job", "Unsuspended node: " + nodeId);
    }
    else
    {
        postToLeaderAsync("/api/nodes/" + nodeId + "/unsuspend", "");
    }
}

namespace {
// Look up a peer's endpoint by nodeId without PeerManager gaining a
// dedicated accessor (the snapshot is cheap — it's a copy of a map).
std::string peerEndpointFor(const std::vector<PeerInfo>& peers,
                            const std::string& nodeId)
{
    for (const auto& p : peers)
        if (p.node_id == nodeId) return p.endpoint;
    return {};
}
} // namespace

namespace {
// Log HTTP round-trip result for peer remote-control actions so a
// silent failure (peer unreachable, 404, auth rejection) shows up in
// the local log instead of making the UI look buggy.
void logPeerResult(const std::string& action, const std::string& nodeId,
                   bool success, const std::string& response)
{
    if (success)
    {
        MonitorLog::instance().info("peer",
            action + " ok: " + nodeId);
    }
    else
    {
        std::string msg = action + " failed: " + nodeId;
        if (!response.empty()) msg += " (" + response + ")";
        MonitorLog::instance().warn("peer", msg);
    }
}
} // namespace

void MonitorApp::setPeerNodeActive(const std::string& nodeId, bool active)
{
    const std::string ep = peerEndpointFor(m_peerManager.getPeerSnapshot(), nodeId);
    if (ep.empty())
    {
        MonitorLog::instance().warn("peer",
            "setPeerNodeActive: unknown or endpointless peer " + nodeId);
        return;
    }

    auto [host, port] = parseEndpoint(ep);
    if (host.empty())
        return;

    // Optimistic local update — peer's next status poll confirms or
    // reverts this.
    m_peerManager.setPeerNodeState(nodeId, active ? "active" : "stopped");

    const std::string label = active ? "start" : "stop";
    HttpRequest req;
    req.host     = host;
    req.port     = port;
    req.method   = "POST";
    req.endpoint = active ? "/api/node/start" : "/api/node/stop";
    req.callback = [label, nodeId](bool ok, const std::string& resp) {
        logPeerResult("peer " + label, nodeId, ok, resp);
    };
    {
        std::lock_guard<std::mutex> lock(m_httpQueueMutex);
        m_httpQueue.push(std::move(req));
    }
}

void MonitorApp::restartPeerApp(const std::string& nodeId)
{
    const std::string ep = peerEndpointFor(m_peerManager.getPeerSnapshot(), nodeId);
    if (ep.empty())
    {
        MonitorLog::instance().warn("peer",
            "restartPeerApp: unknown or endpointless peer " + nodeId);
        return;
    }

    auto [host, port] = parseEndpoint(ep);
    if (host.empty())
        return;

    HttpRequest req;
    req.host     = host;
    req.port     = port;
    req.method   = "POST";
    req.endpoint = "/api/node/restart";
    req.callback = [nodeId](bool ok, const std::string& resp) {
        logPeerResult("peer restart", nodeId, ok, resp);
    };
    {
        std::lock_guard<std::mutex> lock(m_httpQueueMutex);
        m_httpQueue.push(std::move(req));
    }
}

bool MonitorApp::forgetPeer(const std::string& nodeId)
{
    if (!m_farmRunning || nodeId.empty())
        return false;
    if (nodeId == m_identity.nodeId())
    {
        MonitorLog::instance().warn("peer",
            "forgetPeer: refusing to remove own endpoint.json");
        return false;
    }

    std::error_code ec;
    auto endpointPath = m_farmPath / "nodes" / nodeId / "endpoint.json";
    if (std::filesystem::remove(endpointPath, ec))
    {
        MonitorLog::instance().info("peer",
            "Forgot peer (removed endpoint.json): " + nodeId);
        return true;
    }
    MonitorLog::instance().info("peer",
        "forgetPeer: no endpoint.json to remove for " + nodeId);
    return false;
}

bool MonitorApp::writePeerRestartSignal(const std::string& nodeId)
{
    if (!m_farmRunning || nodeId.empty())
        return false;

    std::error_code ec;
    auto nodeDir = m_farmPath / "nodes" / nodeId;
    if (!std::filesystem::is_directory(nodeDir, ec))
    {
        MonitorLog::instance().warn("peer",
            "writePeerRestartSignal: node directory missing for " + nodeId);
        return false;
    }

    auto signalPath = nodeDir / "restart";
    std::ofstream ofs(signalPath);
    if (!ofs.is_open())
    {
        MonitorLog::instance().warn("peer",
            "writePeerRestartSignal: failed to write " + signalPath.string());
        return false;
    }
    // Empty file — presence is the signal.
    MonitorLog::instance().info("peer",
        "Wrote restart signal for " + nodeId);
    return true;
}

// --- Node state ---

void MonitorApp::setNodeState(NodeState state)
{
    m_nodeState = state;
    if (state == NodeState::Stopped)
    {
        m_renderCoordinator.setStopped(true);
        m_renderCoordinator.abortCurrentRender("node stopped");
        m_peerManager.setNodeState("stopped");
    }
    else
    {
        m_renderCoordinator.setStopped(false);
        m_peerManager.setNodeState("active");
    }

    // Persist across restarts
    m_config.node_stopped = (state == NodeState::Stopped);
    saveConfig();
}

// --- Leader endpoint helper ---

std::string MonitorApp::getLeaderEndpoint() const
{
    auto peers = m_peerManager.getPeerSnapshot();
    for (const auto& p : peers)
    {
        if (p.is_leader && p.is_alive)
            return p.endpoint;
    }
    // If we are the leader, return our own endpoint
    if (isLeader())
    {
        std::string localIp = m_config.ip_override.empty()
            ? getLocalIpAddress()
            : m_config.ip_override;
        return localIp + ":" + std::to_string(m_config.http_port);
    }
    return {};
}

// --- Tray state ---

std::string MonitorApp::trayIconName() const
{
    if (!m_farmRunning)
        return "gray";

    if (m_nodeState == NodeState::Stopped)
        return "gray";

    if (!m_agentSupervisor.isAgentConnected())
        return "red";

    if (m_renderCoordinator.isRendering())
        return "green";

    return "blue";
}

std::string MonitorApp::trayTooltip() const
{
    std::string tip = "MinRender";
    if (m_renderCoordinator.isRendering())
        tip += " - Rendering " + m_renderCoordinator.currentJobId();
    else if (m_nodeState == NodeState::Stopped)
        tip += " - Stopped";
    else if (!m_farmRunning)
        tip += " - No Farm";
    else
        tip += " - Idle";
    return tip;
}

std::string MonitorApp::trayStatusText() const
{
    if (m_renderCoordinator.isRendering())
        return "Rendering " + m_renderCoordinator.currentChunkLabel();
    if (m_nodeState == NodeState::Stopped)
        return "Stopped";
    if (!m_farmRunning)
        return "No Farm";
    return "Idle";
}

// --- Exit flow ---

void MonitorApp::requestExit()
{
    m_exitRequested = true;
    if (!m_renderCoordinator.isRendering())
        m_shouldExit = true;
}

void MonitorApp::beginForceExit()
{
    m_renderCoordinator.abortCurrentRender("shutdown");
    m_shouldExit = true;
}

void MonitorApp::cancelExit()
{
    m_exitRequested = false;
}

// --- Restart sidecar ---

bool MonitorApp::launchRestartSidecar()
{
#ifdef _WIN32
    DWORD pid = GetCurrentProcessId();

    wchar_t exePathW[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePathW, MAX_PATH);
    std::filesystem::path exePath(exePathW);

    // Find mr-restart.exe next to monitor exe
    std::filesystem::path restartExe = exePath.parent_path() / "mr-restart.exe";
    if (!std::filesystem::exists(restartExe))
    {
        MonitorLog::instance().error("farm", "mr-restart.exe not found at: " + restartExe.string());
        return false;
    }

    // Cleanup leftover restart.bat from older versions
    {
        auto oldBat = m_appDataDir / "restart.bat";
        std::error_code ec;
        std::filesystem::remove(oldBat, ec);
    }

    // Build command line
    std::wstring cmdLine = L"\"" + restartExe.wstring() + L"\" --pid " +
        std::to_wstring(pid) + L" --exe \"" + exePath.wstring() + L"\" --minimized";

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};

    BOOL ok = CreateProcessW(
        restartExe.wstring().c_str(),
        cmdLine.data(),
        nullptr, nullptr, FALSE,
        CREATE_BREAKAWAY_FROM_JOB | CREATE_NEW_PROCESS_GROUP,
        nullptr, nullptr,
        &si, &pi);

    if (ok)
    {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        MonitorLog::instance().info("farm", "Restart sidecar launched (PID " + std::to_string(pid) + ")");
    }
    else
    {
        MonitorLog::instance().error("farm", "Failed to launch restart sidecar");
        return false;
    }

    // Defer shutdown so mr-restart.exe has time to open process handle
    m_restartPending = true;
    m_restartLaunchedAt = std::chrono::steady_clock::now();
    return true;
#else
    MonitorLog::instance().warn("farm", "Restart sidecar not supported on this platform");
    return false;
#endif
}

// --- Job selection ---

void MonitorApp::selectJob(const std::string& id)
{
    m_selectedJobId = id;
}

void MonitorApp::requestSubmissionMode()
{
    m_requestSubmission = true;
}

bool MonitorApp::shouldEnterSubmission()
{
    bool val = m_requestSubmission;
    m_requestSubmission = false;
    return val;
}

// --- Build local PeerInfo for HTTP responses ---

PeerInfo MonitorApp::buildLocalPeerInfo() const
{
    PeerInfo info;
    info.node_id = m_identity.nodeId();
    info.hostname = m_identity.systemInfo().hostname;
    info.os = getOS();
    info.app_version = APP_VERSION;
    info.gpu_name = m_identity.systemInfo().gpuName;
    info.cpu_cores = m_identity.systemInfo().cpuCores;
    info.ram_mb = m_identity.systemInfo().ramMB;
    info.node_state = (m_nodeState == NodeState::Active) ? "active" : "stopped";
    info.priority = m_config.priority;
    info.tags = m_config.tags;
    info.is_local = true;
    info.is_alive = true;

    // Render state
    if (m_renderCoordinator.isRendering())
    {
        info.render_state = "rendering";
        info.active_job = m_renderCoordinator.currentJobId();
        info.active_chunk = m_renderCoordinator.currentChunkLabel();
    }
    else
    {
        info.render_state = "idle";
    }

    // Agent health
    info.agent_health = m_agentSupervisor.agentHealth();
    if (m_agentSupervisor.agentHealthEnum() != AgentHealth::Ok)
    {
        info.alert_reason = "Agent " + m_agentSupervisor.agentHealth();
        if (!m_agentSupervisor.isAgentConnected())
            info.alert_reason += " (disconnected)";
    }

    // Readiness gating
    info.ready_for_work = m_agentSupervisor.readyForWork();

    // Endpoint
    std::string localIp = m_config.ip_override.empty()
        ? getLocalIpAddress()
        : m_config.ip_override;
    info.endpoint = localIp + ":" + std::to_string(m_config.http_port);

    return info;
}

// --- UDP multicast ---

void MonitorApp::handleUdpMessages()
{
    auto messages = m_udpNotify.poll();
    for (const auto& msg : messages)
    {
        std::string type = msg.value("t", "");
        if (type == "hb")
        {
            std::string nodeId = msg.value("n", "");
            if (nodeId.empty() || nodeId == m_identity.nodeId())
                continue;

            std::string ip = msg.value("ip", "");
            uint16_t port = msg.value("port", uint16_t(8420));
            std::string st = msg.value("st", "active");
            std::string rs = msg.value("rs", "idle");
            std::string job = msg.value("job", "");
            std::string chunk = msg.value("chunk", "");
            int pri = msg.value("pri", 100);
            std::string ah = msg.value("ah", "ok");
            std::string ar = msg.value("ar", "");
            bool rfw = msg.value("rfw", true);

            m_peerManager.processUdpHeartbeat(nodeId, ip, port, st, rs, job, chunk, pri, ah, ar, rfw);
        }
        else if (type == "bye")
        {
            std::string nodeId = msg.value("n", "");
            if (!nodeId.empty() && nodeId != m_identity.nodeId())
                m_peerManager.processUdpGoodbye(nodeId);
        }
    }
}

void MonitorApp::sendUdpHeartbeat()
{
    std::string localIp = m_config.ip_override.empty()
        ? getLocalIpAddress()
        : m_config.ip_override;

    nlohmann::json hb = {
        {"t", "hb"},
        {"from", m_identity.nodeId()},
        {"n", m_identity.nodeId()},
        {"ip", localIp},
        {"port", m_config.http_port},
        {"st", (m_nodeState == NodeState::Active) ? "active" : "stopped"},
        {"rs", m_renderCoordinator.isRendering() ? "rendering" : "idle"},
        {"pri", m_config.priority},
        {"ah", m_agentSupervisor.agentHealth()},
        {"rfw", m_agentSupervisor.readyForWork()},
    };

    if (m_renderCoordinator.isRendering())
    {
        hb["job"] = m_renderCoordinator.currentJobId();
        hb["chunk"] = m_renderCoordinator.currentChunkLabel();
    }

    if (m_agentSupervisor.agentHealthEnum() != AgentHealth::Ok)
    {
        std::string reason = "Agent " + m_agentSupervisor.agentHealth();
        if (!m_agentSupervisor.isAgentConnected())
            reason += " (disconnected)";
        hb["ar"] = reason;
    }

    m_udpNotify.send(hb);
}

// --- Background HTTP worker ---

void MonitorApp::postToLeaderAsync(const std::string& endpoint, const std::string& body,
                                    std::function<void(bool, const std::string&)> callback,
                                    const std::string& method)
{
    std::string leaderEp = getLeaderEndpoint();
    if (leaderEp.empty())
    {
        if (callback) callback(false, "");
        return;
    }

    auto [host, port] = parseEndpoint(leaderEp);
    if (host.empty())
    {
        if (callback) callback(false, "");
        return;
    }

    HttpRequest req;
    req.host = host;
    req.port = port;
    req.method = method;
    req.endpoint = endpoint;
    req.body = body;
    req.callback = std::move(callback);

    {
        std::lock_guard<std::mutex> lock(m_httpQueueMutex);
        m_httpQueue.push(std::move(req));
    }
}

void MonitorApp::startHttpWorker()
{
    m_httpWorkerRunning.store(true);
    m_httpWorkerThread = std::thread(&MonitorApp::httpWorkerLoop, this);
}

void MonitorApp::stopHttpWorker()
{
    m_httpWorkerRunning.store(false);
    if (m_httpWorkerThread.joinable())
        m_httpWorkerThread.join();
}

void MonitorApp::httpWorkerLoop()
{
    auto reportCooldown = std::chrono::steady_clock::time_point{};
    auto lastFrameFlush = std::chrono::steady_clock::now();

    while (m_httpWorkerRunning.load())
    {
        // 1. Process one-off requests from queue (job controls, submissions)
        {
            std::unique_lock<std::mutex> lock(m_httpQueueMutex);
            while (!m_httpQueue.empty())
            {
                auto req = std::move(m_httpQueue.front());
                m_httpQueue.pop();
                lock.unlock();

                bool success = false;
                std::string response;
                try
                {
                    httplib::Client cli(req.host, req.port);
                    cli.set_connection_timeout(0, 500000); // 500ms
                    cli.set_read_timeout(2);
                    auto hdrs = authHeaders(m_farmSecret);

                    httplib::Result res = (req.method == "DELETE")
                        ? cli.Delete(req.endpoint, hdrs)
                        : cli.Post(req.endpoint, hdrs, req.body, "application/json");

                    if (res)
                    {
                        success = (res->status == 200);
                        response = res->body;
                    }
                }
                catch (...) {}

                if (req.callback)
                    req.callback(success, response);

                lock.lock();
            }
        }

        auto now = std::chrono::steady_clock::now();

        // 2. Flush completion reports (respecting cooldown)
        if (now >= reportCooldown && m_farmRunning)
        {
            if (flushCompletionReports())
                reportCooldown = now + std::chrono::seconds(5);
        }

        // 3. Flush frame reports every 2s (respecting cooldown)
        auto frameElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - lastFrameFlush).count();
        if (frameElapsed >= 2000 && now >= reportCooldown && m_farmRunning)
        {
            if (flushFrameReports())
                reportCooldown = now + std::chrono::seconds(5);
            lastFrameFlush = now;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

bool MonitorApp::flushCompletionReports()
{
    std::vector<PendingReport> batch;
    {
        std::lock_guard<std::mutex> lock(m_reportMutex);
        if (m_pendingReports.empty()) return false;
        batch = std::move(m_pendingReports);
        m_pendingReports.clear();
    }

    std::string leaderEp = getLeaderEndpoint();
    if (leaderEp.empty())
    {
        std::lock_guard<std::mutex> lock(m_reportMutex);
        m_pendingReports.insert(m_pendingReports.begin(), batch.begin(), batch.end());
        return false;
    }

    auto [host, port] = parseEndpoint(leaderEp);
    if (host.empty())
    {
        std::lock_guard<std::mutex> lock(m_reportMutex);
        m_pendingReports.insert(m_pendingReports.begin(), batch.begin(), batch.end());
        return false;
    }

    bool anyFailed = false;
    std::vector<PendingReport> unsent;

    for (auto& report : batch)
    {
        if (anyFailed)
        {
            unsent.push_back(std::move(report));
            continue;
        }

        try
        {
            httplib::Client cli(host, port);
            cli.set_connection_timeout(0, 500000); // 500ms
            cli.set_read_timeout(1);

            nlohmann::json body = {
                {"node_id", m_identity.nodeId()},
                {"job_id", report.jobId},
                {"frame_start", report.frameStart},
                {"frame_end", report.frameEnd},
            };

            std::string endpoint;
            if (report.state == "completed")
            {
                body["elapsed_ms"] = report.elapsedMs;
                body["exit_code"] = report.exitCode;
                endpoint = "/api/dispatch/complete";
            }
            else
            {
                body["error"] = report.error;
                endpoint = "/api/dispatch/failed";
            }

            auto res = cli.Post(endpoint, authHeaders(m_farmSecret), body.dump(), "application/json");
            if (!res || res->status != 200)
            {
                anyFailed = true;
                unsent.push_back(std::move(report));
            }
        }
        catch (...)
        {
            anyFailed = true;
            unsent.push_back(std::move(report));
        }
    }

    if (!unsent.empty())
    {
        std::lock_guard<std::mutex> lock(m_reportMutex);
        // Prepend unsent reports before any new reports that arrived during flush
        unsent.insert(unsent.end(),
            std::make_move_iterator(m_pendingReports.begin()),
            std::make_move_iterator(m_pendingReports.end()));
        m_pendingReports = std::move(unsent);
    }

    return anyFailed;
}

bool MonitorApp::flushFrameReports()
{
    std::vector<PendingFrameReport> batch;
    {
        std::lock_guard<std::mutex> lock(m_reportMutex);
        if (m_pendingFrameReports.empty()) return false;
        batch = std::move(m_pendingFrameReports);
        m_pendingFrameReports.clear();
    }

    std::string leaderEp = getLeaderEndpoint();
    if (leaderEp.empty())
    {
        std::lock_guard<std::mutex> lock(m_reportMutex);
        m_pendingFrameReports.insert(m_pendingFrameReports.begin(), batch.begin(), batch.end());
        return false;
    }

    auto [host, port] = parseEndpoint(leaderEp);
    if (host.empty())
    {
        std::lock_guard<std::mutex> lock(m_reportMutex);
        m_pendingFrameReports.insert(m_pendingFrameReports.begin(), batch.begin(), batch.end());
        return false;
    }

    // Group by job
    std::unordered_map<std::string, std::vector<int>> byJob;
    for (const auto& fr : batch)
        byJob[fr.jobId].push_back(fr.frame);

    bool allSent = true;
    for (auto& [jobId, frames] : byJob)
    {
        try
        {
            httplib::Client cli(host, port);
            cli.set_connection_timeout(0, 500000); // 500ms
            cli.set_read_timeout(1);

            nlohmann::json body = {
                {"node_id", m_identity.nodeId()},
                {"job_id", jobId},
                {"frames", frames},
            };

            auto res = cli.Post("/api/dispatch/frame-complete",
                authHeaders(m_farmSecret), body.dump(), "application/json");
            if (!res || res->status != 200)
                allSent = false;
        }
        catch (...)
        {
            allSent = false;
        }
    }

    if (!allSent)
    {
        std::lock_guard<std::mutex> lock(m_reportMutex);
        m_pendingFrameReports.insert(m_pendingFrameReports.begin(), batch.begin(), batch.end());
    }

    return !allSent;
}

} // namespace MR

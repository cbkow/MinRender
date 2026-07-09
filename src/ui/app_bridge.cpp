#include "ui/app_bridge.h"

#include "core/config.h"
#include "core/monitor_log.h"
#include "core/path_mapping.h"
#include "core/platform.h"
#include "monitor/dispatch_manager.h"
#include "monitor/monitor_app.h"
#include "monitor/peer_manager.h"
#include "monitor/template_manager.h"
#include "ui/models/chunks_model.h"
#include "ui/models/jobs_model.h"
#include "ui/models/log_model.h"
#include "ui/models/nodes_model.h"
#include "ui/models/templates_model.h"
#include "ui/platform/accent_color.h"

#ifdef MINRENDER_HAVE_PREVIEW
#include "preview/exr_image_loader.h"
#endif

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <algorithm>
#include <ctime>
#include <deque>
#include <regex>
#include <set>
#include <filesystem>
#include <fstream>
#include <map>
#include <nlohmann/json.hpp>

namespace MR {

namespace {

QStringList splitTagsCsv(const QString& csv)
{
    const QStringList raw = csv.split(QLatin1Char(','), Qt::SkipEmptyParts);
    QStringList out;
    out.reserve(raw.size());
    for (const QString& tag : raw)
    {
        const QString trimmed = tag.trimmed();
        if (!trimmed.isEmpty())
            out.push_back(trimmed);
    }
    return out;
}

} // namespace

AppBridge::AppBridge(MonitorApp* monitor, QObject* parent)
    : QObject(parent)
    , m_monitor(monitor)
    , m_accentColor(systemAccentColor())
    , m_jobsModel(std::make_unique<JobsModel>())
    , m_nodesModel(std::make_unique<NodesModel>())
    , m_logModel(std::make_unique<LogModel>())
    , m_chunksModel(std::make_unique<ChunksModel>())
    , m_templatesModel(std::make_unique<TemplatesModel>())
    , m_lastFarmRunning(monitor ? monitor->isFarmRunning() : false)
    , m_lastIsLeader(monitor ? monitor->isLeader() : false)
    , m_lastNodeActive(monitor ? monitor->nodeState() == MR::NodeState::Active : true)
{
    takeSnapshot();
    if (m_monitor)
    {
        m_jobsModel->setJobs(m_monitor->cachedJobs());
        m_nodesModel->setPeers(m_monitor->peerManager().getPeerSnapshot());
        m_templatesModel->setTemplates(m_monitor->cachedTemplates());
    }
    // attach() seeds from MonitorLog's existing ring and installs the
    // callback — safe to call even if MonitorApp::init() hasn't run yet,
    // since MonitorLog is a singleton that's always available.
    m_logModel->attach();

    // Chunks refresh cadence matches the existing ImGui panel (3 s).
    // Timer only runs while a job is selected; setCurrentJobId starts /
    // stops it.
    m_chunksTimer.setInterval(3000);
    QObject::connect(&m_chunksTimer, &QTimer::timeout,
                     this, &AppBridge::refreshChunks);

    // Remote log refresh — same 3 s cadence, only runs while the log
    // viewer is pointed at a peer or at task output (logSourceId
    // non-empty). refreshLogSource routes to the right handler.
    m_remoteLogTimer.setInterval(3000);
    QObject::connect(&m_remoteLogTimer, &QTimer::timeout,
                     this, &AppBridge::refreshLogSource);

    // Seed the log-source dropdown so the ComboBox renders "Monitor Log"
    // on first paint even before the first peer poll.
    rebuildLogSources();
}

AppBridge::~AppBridge() = default;

void AppBridge::takeSnapshot()
{
    if (m_monitor)
        m_snapshot = m_monitor->config();
}

void AppBridge::emitAllSettingsChanged()
{
    emit syncRootChanged();
    emit tagsCsvChanged();
    emit httpPortChanged();
    emit ipOverrideChanged();
    emit udpEnabledChanged();
    emit udpPortChanged();
    emit showNotificationsChanged();
    emit stagingEnabledChanged();
    emit rndrDualModeChanged();
}

bool AppBridge::farmRunning() const
{
    return m_monitor ? m_monitor->isFarmRunning() : false;
}

QString AppBridge::syncRoot() const
{
    return m_monitor ? QString::fromStdString(m_monitor->config().sync_root) : QString();
}

void AppBridge::setSyncRoot(const QString& v)
{
    if (!m_monitor) return;
    const std::string s = v.toStdString();
    if (m_monitor->config().sync_root == s) return;
    m_monitor->config().sync_root = s;
    emit syncRootChanged();
}

QString AppBridge::tagsCsv() const
{
    if (!m_monitor) return {};
    const auto& tags = m_monitor->config().tags;
    QStringList qs;
    qs.reserve(static_cast<int>(tags.size()));
    for (const auto& t : tags)
        qs.push_back(QString::fromStdString(t));
    return qs.join(QStringLiteral(", "));
}

void AppBridge::setTagsCsv(const QString& v)
{
    if (!m_monitor) return;
    const QStringList parsed = splitTagsCsv(v);

    std::vector<std::string> newTags;
    newTags.reserve(parsed.size());
    for (const QString& t : parsed)
        newTags.push_back(t.toStdString());

    if (m_monitor->config().tags == newTags) return;
    m_monitor->config().tags = std::move(newTags);
    emit tagsCsvChanged();
}

int AppBridge::httpPort() const
{
    return m_monitor ? static_cast<int>(m_monitor->config().http_port) : 0;
}

void AppBridge::setHttpPort(int v)
{
    if (!m_monitor) return;
    const int clamped = std::clamp(v, 1, 65535);
    const auto u16 = static_cast<uint16_t>(clamped);
    if (m_monitor->config().http_port == u16) return;
    m_monitor->config().http_port = u16;
    emit httpPortChanged();
}

QString AppBridge::ipOverride() const
{
    return m_monitor ? QString::fromStdString(m_monitor->config().ip_override) : QString();
}

void AppBridge::setIpOverride(const QString& v)
{
    if (!m_monitor) return;
    const std::string s = v.toStdString();
    if (m_monitor->config().ip_override == s) return;
    m_monitor->config().ip_override = s;
    emit ipOverrideChanged();
}

bool AppBridge::udpEnabled() const
{
    return m_monitor ? m_monitor->config().udp_enabled : false;
}

void AppBridge::setUdpEnabled(bool v)
{
    if (!m_monitor) return;
    if (m_monitor->config().udp_enabled == v) return;
    m_monitor->config().udp_enabled = v;
    emit udpEnabledChanged();
}

int AppBridge::udpPort() const
{
    return m_monitor ? static_cast<int>(m_monitor->config().udp_port) : 0;
}

void AppBridge::setUdpPort(int v)
{
    if (!m_monitor) return;
    const int clamped = std::clamp(v, 1, 65535);
    const auto u16 = static_cast<uint16_t>(clamped);
    if (m_monitor->config().udp_port == u16) return;
    m_monitor->config().udp_port = u16;
    emit udpPortChanged();
}

bool AppBridge::showNotifications() const
{
    return m_monitor ? m_monitor->config().show_notifications : false;
}

void AppBridge::setShowNotifications(bool v)
{
    if (!m_monitor) return;
    if (m_monitor->config().show_notifications == v) return;
    m_monitor->config().show_notifications = v;
    emit showNotificationsChanged();
}

bool AppBridge::stagingEnabled() const
{
    return m_monitor ? m_monitor->config().staging_enabled : false;
}

void AppBridge::setStagingEnabled(bool v)
{
    if (!m_monitor) return;
    if (m_monitor->config().staging_enabled == v) return;
    m_monitor->config().staging_enabled = v;
    emit stagingEnabledChanged();
}

bool AppBridge::rndrDualMode() const
{
    return m_monitor ? m_monitor->config().rndr_dual_mode : false;
}

void AppBridge::setRndrDualMode(bool v)
{
    if (!m_monitor) return;
    if (m_monitor->config().rndr_dual_mode == v) return;
    m_monitor->config().rndr_dual_mode = v;
    emit rndrDualModeChanged();
}

bool AppBridge::rndrAvailable() const
{
    return m_monitor ? m_monitor->rndrSupervisor().isBinaryAvailable() : false;
}

QString AppBridge::rndrStatus() const
{
    return m_monitor
        ? QString::fromStdString(m_monitor->rndrSupervisor().statusText())
        : QString();
}

void AppBridge::setCurrentJobId(const QString& jobId)
{
    if (m_currentJobId == jobId)
        return;
    m_currentJobId = jobId;
    emit currentJobIdChanged();
    emit currentJobChanged();

    // Picking a real job cancels submission mode — user moved on.
    if (!jobId.isEmpty() && m_submissionMode)
        setSubmissionMode(false);

    // A pinned preview frame belongs to the previous job.
    clearPreviewPin();

    // Keep MonitorApp's selected-job state in sync — HTTP handlers
    // and render logic key off selectedJobId() for some behavior.
    if (m_monitor)
        m_monitor->selectJob(m_currentJobId.toStdString());

    if (m_currentJobId.isEmpty())
    {
        m_chunksTimer.stop();
        m_chunksModel->clear();
    }
    else
    {
        // Fetch immediately; timer fires every 3 s after that.
        refreshChunks();
        m_chunksTimer.start();
    }

    // If the log viewer is in Task Output mode, refresh its chunks
    // against the new job. Clears selection via refreshTaskOutput's
    // structural-change branch when the file set changes.
    if (m_logSourceId == QStringLiteral("__task__"))
    {
        m_taskOutputChunks.clear();
        emit taskOutputChunksChanged();
        m_selectedTaskChunkIndex = -1;
        emit selectedTaskChunkIndexChanged();
        m_taskOutputLines.clear();
        emit taskOutputLinesChanged();
        refreshTaskOutput();
    }
}

void AppBridge::setSubmissionMode(bool on)
{
    if (m_submissionMode == on)
        return;
    m_submissionMode = on;
    emit submissionModeChanged();
}

void AppBridge::setJobsRefreshPaused(bool paused)
{
    if (m_jobsRefreshPaused == paused)
        return;
    m_jobsRefreshPaused = paused;
    emit jobsRefreshPausedChanged();
    // Catch up immediately when the drag ends.
    if (!paused && m_monitor)
        m_jobsModel->setJobs(m_monitor->cachedJobs());
}

QVariantMap AppBridge::currentJob() const
{
    if (!m_monitor || m_currentJobId.isEmpty())
        return {};
    const std::string id = m_currentJobId.toStdString();
    const auto& jobs = m_monitor->cachedJobs();
    auto it = std::find_if(jobs.begin(), jobs.end(),
        [&](const JobInfo& j) { return j.manifest.job_id == id; });
    if (it == jobs.end())
        return {};

    const JobInfo& j = *it;
    QVariantMap m;
    m["jobId"]           = QString::fromStdString(j.manifest.job_id);
    m["name"]            = QString::fromStdString(j.manifest.job_id);
    m["state"]           = QString::fromStdString(j.current_state);
    m["priority"]        = j.current_priority;
    m["totalChunks"]     = j.total_chunks;
    m["doneChunks"]      = j.completed_chunks;
    m["failedChunks"]    = j.failed_chunks;
    m["renderingChunks"] = j.rendering_chunks;
    m["progress"]        = j.total_chunks > 0
                           ? static_cast<double>(j.completed_chunks)
                             / static_cast<double>(j.total_chunks)
                           : 0.0;
    m["createdAt"]       = static_cast<qint64>(j.manifest.submitted_at_ms);
    m["frameStart"]      = j.manifest.frame_start;
    m["frameEnd"]        = j.manifest.frame_end;
    return m;
}

void AppBridge::refreshChunks()
{
    if (!m_monitor || m_currentJobId.isEmpty())
        return;

    // Resolve node ids to hostnames for the chunk table's Node column.
    // Ids missing from the map (peer since forgotten / offline) fall
    // back to the raw id inside the model.
    QHash<QString, QString> names;
    if (!thisNodeHostname().isEmpty())
        names.insert(thisNodeId(), thisNodeHostname());
    for (const auto& p : m_monitor->peerManager().getPeerSnapshot())
    {
        const QString host = QString::fromStdString(p.hostname);
        if (!host.isEmpty())
            names.insert(QString::fromStdString(p.node_id), host);
    }
    m_chunksModel->setNodeNames(names);

    // On workers this is a blocking HTTP GET to the leader (~1.5 s
    // worst case). Phase 5's frame grid may promote this to the async
    // postToLeaderAsync path if the UI stalls become noticeable.
    m_chunksModel->setChunks(
        m_monitor->getChunksForJob(m_currentJobId.toStdString()));
}

void AppBridge::setLogSourceId(const QString& id)
{
    if (m_logSourceId == id)
        return;
    m_logSourceId = id;
    emit logSourceIdChanged();

    // Clear buffers from the previous mode so the panel doesn't flash
    // stale content at the new mode's empty state.
    if (!m_remoteLogLines.isEmpty())
    {
        m_remoteLogLines.clear();
        emit remoteLogLinesChanged();
    }
    if (!m_taskOutputChunks.isEmpty())
    {
        m_taskOutputChunks.clear();
        emit taskOutputChunksChanged();
    }
    if (m_selectedTaskChunkIndex != -1)
    {
        m_selectedTaskChunkIndex = -1;
        emit selectedTaskChunkIndexChanged();
    }
    if (!m_taskOutputLines.isEmpty())
    {
        m_taskOutputLines.clear();
        emit taskOutputLinesChanged();
    }

    if (m_logSourceId.isEmpty())
    {
        m_remoteLogTimer.stop();
    }
    else
    {
        refreshLogSource();   // immediate; timer fires every 3 s after
        m_remoteLogTimer.start();
    }
}

void AppBridge::refreshLogSource()
{
    if (m_logSourceId.isEmpty())
        return;
    if (m_logSourceId == QStringLiteral("__task__"))
        refreshTaskOutput();
    else
        refreshRemoteLog();
}

void AppBridge::refreshRemoteLog()
{
    if (!m_monitor || m_logSourceId.isEmpty() || !m_monitor->isFarmRunning())
        return;

    const auto lines = MonitorLog::readNodeLog(
        m_monitor->farmPath(), m_logSourceId.toStdString(), 500);

    QStringList next;
    next.reserve(static_cast<int>(lines.size()));
    for (const auto& l : lines)
        next.push_back(QString::fromStdString(l));

    if (next == m_remoteLogLines)
        return;   // skip a churn-inducing emit when the file hasn't grown
    m_remoteLogLines = std::move(next);
    emit remoteLogLinesChanged();
}

void AppBridge::refreshTaskOutput()
{
    if (!m_monitor || m_currentJobId.isEmpty() || !m_monitor->isFarmRunning())
        return;

    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path stdoutDir = m_monitor->farmPath()
        / "jobs" / m_currentJobId.toStdString() / "stdout";

    if (!fs::is_directory(stdoutDir, ec))
    {
        if (!m_taskOutputChunks.isEmpty())
        {
            m_taskOutputChunks.clear();
            emit taskOutputChunksChanged();
        }
        return;
    }

    QVariantList chunks;
    for (const auto& nodeEntry : fs::directory_iterator(stdoutDir, ec))
    {
        if (!nodeEntry.is_directory()) continue;
        const std::string nodeId = nodeEntry.path().filename().string();

        for (const auto& fileEntry : fs::directory_iterator(nodeEntry.path(), ec))
        {
            if (!fileEntry.is_regular_file()) continue;
            const std::string fname = fileEntry.path().filename().string();
            if (fname.size() < 5 || fname.substr(fname.size() - 4) != ".log")
                continue;

            // Format: {rangeStr}_{timestamp_ms}.log
            const std::string stem = fname.substr(0, fname.size() - 4);
            const auto underPos = stem.rfind('_');
            if (underPos == std::string::npos) continue;

            qint64 ts = 0;
            try { ts = std::stoll(stem.substr(underPos + 1)); }
            catch (...) { continue; }

            const std::string rangeStr = stem.substr(0, underPos);

            // Display: "f{range}  HH:MM:SS" on {hostname}. No host here
            // (we only have nodeId); LogPanel can decorate further.
            char timeBuf[16] = {0};
            time_t t = static_cast<time_t>(ts / 1000);
            struct tm tmBuf;
#ifdef _WIN32
            localtime_s(&tmBuf, &t);
#else
            localtime_r(&t, &tmBuf);
#endif
            std::strftime(timeBuf, sizeof(timeBuf), "%H:%M:%S", &tmBuf);

            QVariantMap entry;
            entry["nodeId"]       = QString::fromStdString(nodeId);
            entry["rangeStr"]     = QString::fromStdString(rangeStr);
            entry["timestampMs"]  = ts;
            entry["path"]         = QString::fromStdString(fileEntry.path().string());
            entry["displayLabel"] = QString("f%1  %2")
                .arg(QString::fromStdString(rangeStr))
                .arg(QString::fromLatin1(timeBuf));
            chunks.push_back(std::move(entry));
        }
    }

    // Sort by range, then timestamp (older renders first).
    std::sort(chunks.begin(), chunks.end(),
        [](const QVariant& a, const QVariant& b) {
            const auto am = a.toMap();
            const auto bm = b.toMap();
            const QString ra = am.value("rangeStr").toString();
            const QString rb = bm.value("rangeStr").toString();
            if (ra != rb) return ra < rb;
            return am.value("timestampMs").toLongLong()
                 < bm.value("timestampMs").toLongLong();
        });

    // Compute path of previously-selected chunk so we can preserve
    // selection across refreshes even if the list re-orders.
    QString prevPath;
    if (m_selectedTaskChunkIndex >= 0
        && m_selectedTaskChunkIndex < m_taskOutputChunks.size())
    {
        prevPath = m_taskOutputChunks[m_selectedTaskChunkIndex]
                    .toMap().value("path").toString();
    }

    const bool structurallyChanged = (chunks != m_taskOutputChunks);
    if (structurallyChanged)
    {
        m_taskOutputChunks = std::move(chunks);
        emit taskOutputChunksChanged();

        int newIndex = -1;
        if (!prevPath.isEmpty())
        {
            for (int i = 0; i < m_taskOutputChunks.size(); ++i)
            {
                if (m_taskOutputChunks[i].toMap().value("path").toString() == prevPath)
                {
                    newIndex = i;
                    break;
                }
            }
        }
        if (newIndex != m_selectedTaskChunkIndex)
        {
            m_selectedTaskChunkIndex = newIndex;
            emit selectedTaskChunkIndexChanged();
        }
    }

    // Keep the selected file's contents fresh — the chunk is still
    // rendering, so the .log grows over time.
    if (m_selectedTaskChunkIndex >= 0)
        reloadSelectedTaskChunk();
}

void AppBridge::setSelectedTaskChunkIndex(int i)
{
    if (m_selectedTaskChunkIndex == i) return;
    m_selectedTaskChunkIndex = i;
    emit selectedTaskChunkIndexChanged();
    reloadSelectedTaskChunk();
}

void AppBridge::reloadSelectedTaskChunk()
{
    if (m_selectedTaskChunkIndex < 0
        || m_selectedTaskChunkIndex >= m_taskOutputChunks.size())
    {
        if (!m_taskOutputLines.isEmpty())
        {
            m_taskOutputLines.clear();
            emit taskOutputLinesChanged();
        }
        return;
    }

    const QString path = m_taskOutputChunks[m_selectedTaskChunkIndex]
        .toMap().value("path").toString();
    if (path.isEmpty())
        return;

    QStringList next;
    std::ifstream ifs(path.toStdString());
    std::string line;
    while (std::getline(ifs, line))
        next.push_back(QString::fromStdString(line));

    if (next == m_taskOutputLines)
        return;
    m_taskOutputLines = std::move(next);
    emit taskOutputLinesChanged();
}

void AppBridge::rebuildLogSources()
{
    QVariantList next;
    next.reserve(16);

    // Head entry — always present, always selectable. Empty id signals
    // "show the local MonitorLog ring buffer" to the panel.
    QVariantMap local;
    local["id"]    = QString();
    local["label"] = tr("Monitor Log");
    next.push_back(local);

    // Task Output — per-chunk stdout for the selected job. Sentinel id.
    QVariantMap task;
    task["id"]    = QStringLiteral("__task__");
    task["label"] = tr("Task Output");
    next.push_back(task);

    if (m_monitor)
    {
        // Local node as a peer-style entry so the user can view their
        // own on-disk log file (useful for history before the process
        // started).
        QVariantMap self;
        self["id"]    = QString::fromStdString(m_monitor->identity().nodeId());
        QString selfLabel = QString::fromStdString(
            m_monitor->identity().systemInfo().hostname);
        if (selfLabel.isEmpty())
            selfLabel = self["id"].toString();
        self["label"] = tr("%1 (this node)").arg(selfLabel);
        next.push_back(self);

        for (const auto& p : m_monitor->peerManager().getPeerSnapshot())
        {
            if (!p.is_alive) continue;
            QVariantMap e;
            e["id"]    = QString::fromStdString(p.node_id);
            e["label"] = QString::fromStdString(
                p.hostname.empty() ? p.node_id : p.hostname);
            next.push_back(e);
        }
    }

    if (next == m_logSources)
        return;   // short-circuit when peer list hasn't changed
    m_logSources = std::move(next);
    emit logSourcesChanged();
}

void AppBridge::refresh()
{
    if (!m_monitor) return;

    const bool running = m_monitor->isFarmRunning();
    if (running != m_lastFarmRunning)
    {
        m_lastFarmRunning = running;
        emit farmRunningChanged();
    }

    const bool leaderNow = m_monitor->isLeader();
    if (leaderNow != m_lastIsLeader)
    {
        m_lastIsLeader = leaderNow;
        emit thisNodeIsLeaderChanged();
    }

    const bool activeNow =
        m_monitor->nodeState() == MR::NodeState::Active;
    if (activeNow != m_lastNodeActive)
    {
        m_lastNodeActive = activeNow;
        emit thisNodeActiveChanged();
    }

    // RNDR supervisor status text changes as it cycles through its state
    // machine; surface it live under the Settings-panel toggle.
    const QString rndrNow = rndrStatus();
    if (rndrNow != m_lastRndrStatus)
    {
        m_lastRndrStatus = rndrNow;
        emit rndrStatusChanged();
    }

    // MonitorApp::refreshCachedJobs runs on the same thread (the 50 ms
    // QTimer that calls this method), so direct push is safe. The model
    // diffs internally and only emits dataChanged for changed rows.
    // Skipped mid-drag (jobsRefreshPaused) so a reorder in progress
    // isn't reset under the cursor.
    if (!m_jobsRefreshPaused)
        m_jobsModel->setJobs(m_monitor->cachedJobs());

    // The selected job's field values (progress, failed count, etc.)
    // may have moved even when its row didn't reorder; let JobDetail
    // rebind on every refresh. The map copy is small and the update
    // rate is 20 Hz.
    if (!m_currentJobId.isEmpty())
        emit currentJobChanged();

    // PeerManager::getPeerSnapshot takes a mutex under the hood so it's
    // safe from any thread; we call it from the UI thread anyway.
    m_nodesModel->setPeers(m_monitor->peerManager().getPeerSnapshot());

    // Template cache is refreshed alongside jobs (every 2 s in
    // MonitorApp::update). TemplatesModel short-circuits when the set
    // is equivalent, so polling here at 20 Hz is essentially free.
    // DCC plugin templates stay out of the New Job picker — they're in
    // the cache only so templateById can resolve them for job editing.
    {
        auto templates = m_monitor->cachedTemplates();
        templates.erase(
            std::remove_if(templates.begin(), templates.end(),
                           [](const JobTemplate& t) { return t.isPlugin; }),
            templates.end());
        m_templatesModel->setTemplates(templates);
    }

    // Keep the log-source dropdown in sync with the peer list. The
    // rebuild short-circuits when nothing changed, so this is cheap.
    rebuildLogSources();
}

void AppBridge::revertSettings()
{
    if (!m_monitor) return;
    m_monitor->config() = m_snapshot;
    emitAllSettingsChanged();
}

void AppBridge::saveSettings()
{
    if (!m_monitor) return;

    // Mirror the /api/config handler: fields that affect the farm's
    // shared state or listening ports trigger a deferred farm restart
    // on the next update() tick. Without this, changing sync_root or
    // an HTTP/UDP port through the Settings dialog wouldn't take
    // effect until the user relaunched.
    const Config& now = m_monitor->config();
    const bool needsRestart =
           now.sync_root   != m_snapshot.sync_root
        || now.http_port   != m_snapshot.http_port
        || now.udp_enabled != m_snapshot.udp_enabled
        || now.udp_port    != m_snapshot.udp_port;

    m_monitor->saveConfig();
    takeSnapshot();

    if (needsRestart)
        m_monitor->requestFarmRestart();
}

QString AppBridge::pathMappingsJson() const
{
    if (!m_monitor) return QStringLiteral("[]");
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& m : m_monitor->config().path_mappings)
        arr.push_back(m);
    return QString::fromStdString(arr.dump());
}

void AppBridge::setPathMappings(const QString& json)
{
    if (!m_monitor) return;

    std::vector<PathMapping> parsed;
    try
    {
        auto j = nlohmann::json::parse(json.toStdString());
        if (!j.is_array())
            return;
        parsed.reserve(j.size());
        for (const auto& item : j)
            parsed.push_back(item.get<PathMapping>());
    }
    catch (const std::exception& e)
    {
        // Malformed JSON from the dialog — leave config untouched. The
        // dialog stringifies its own model, so this should only fire if
        // someone calls setPathMappings via QML console with bad input.
        MonitorLog::instance().warn(
            "AppBridge",
            std::string("setPathMappings: failed to parse JSON: ") + e.what());
        return;
    }

    m_monitor->config().path_mappings = std::move(parsed);
    m_monitor->saveConfig();
    emit pathMappingsChanged();
}

QString AppBridge::thisNodeId() const
{
    return m_monitor
        ? QString::fromStdString(m_monitor->identity().nodeId())
        : QString();
}

QString AppBridge::thisNodeHostname() const
{
    return m_monitor
        ? QString::fromStdString(m_monitor->identity().systemInfo().hostname)
        : QString();
}

QString AppBridge::thisNodeGpu() const
{
    return m_monitor
        ? QString::fromStdString(m_monitor->identity().systemInfo().gpuName)
        : QString();
}

int AppBridge::thisNodeCpuCores() const
{
    return m_monitor ? m_monitor->identity().systemInfo().cpuCores : 0;
}

qint64 AppBridge::thisNodeRamMb() const
{
    return m_monitor
        ? static_cast<qint64>(m_monitor->identity().systemInfo().ramMB)
        : 0;
}

bool AppBridge::thisNodeIsLeader() const
{
    return m_monitor ? m_monitor->isLeader() : false;
}

bool AppBridge::thisNodeActive() const
{
    return m_monitor
        ? m_monitor->nodeState() == MR::NodeState::Active
        : true;
}

void AppBridge::toggleNodeActive()
{
    if (!m_monitor) return;
    m_monitor->setNodeState(
        m_monitor->nodeState() == MR::NodeState::Active
            ? MR::NodeState::Stopped
            : MR::NodeState::Active);
}

void AppBridge::unsuspendNode(const QString& nodeId)
{
    if (!m_monitor || nodeId.isEmpty()) return;
    m_monitor->unsuspendNode(nodeId.toStdString());
}

void AppBridge::setPeerNodeActive(const QString& nodeId, bool active)
{
    if (!m_monitor || nodeId.isEmpty()) return;
    m_monitor->setPeerNodeActive(nodeId.toStdString(), active);
}

void AppBridge::restartPeerApp(const QString& nodeId)
{
    if (!m_monitor || nodeId.isEmpty()) return;
    m_monitor->restartPeerApp(nodeId.toStdString());
}

void AppBridge::writePeerRestartSignal(const QString& nodeId)
{
    if (!m_monitor || nodeId.isEmpty()) return;
    m_monitor->writePeerRestartSignal(nodeId.toStdString());
}

void AppBridge::forgetPeer(const QString& nodeId)
{
    if (!m_monitor || nodeId.isEmpty()) return;
    m_monitor->forgetPeer(nodeId.toStdString());
}

QVariantMap AppBridge::scanFarmCleanup()
{
    if (!m_monitor) return {};
    const auto j = m_monitor->scanFarmCleanup();
    // nlohmann::json → QJsonDocument → QVariantMap gives us the same
    // nested QVariantList-of-QVariantMap shape QML expects from the
    // other Q_INVOKABLE methods on this class.
    const QByteArray bytes = QByteArray::fromStdString(j.dump());
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return {};
    return doc.object().toVariantMap();
}

int AppBridge::executeFarmCleanup(const QString& action,
                                  const QStringList& ids)
{
    if (!m_monitor || action.isEmpty()) return 0;
    std::vector<std::string> stdIds;
    stdIds.reserve(ids.size());
    for (const QString& s : ids) stdIds.push_back(s.toStdString());
    return m_monitor->executeFarmCleanup(action.toStdString(), stdIds);
}

void AppBridge::pauseJob(const QString& jobId)
{
    if (!m_monitor || jobId.isEmpty()) return;
    m_monitor->pauseJob(jobId.toStdString());
}

void AppBridge::resumeJob(const QString& jobId)
{
    if (!m_monitor || jobId.isEmpty()) return;
    m_monitor->resumeJob(jobId.toStdString());
}

void AppBridge::cancelJob(const QString& jobId)
{
    if (!m_monitor || jobId.isEmpty()) return;
    m_monitor->cancelJob(jobId.toStdString());
}

void AppBridge::moveJob(const QString& jobId, const QString& targetId, bool before)
{
    if (!m_monitor || jobId.isEmpty() || targetId.isEmpty()) return;
    m_monitor->moveJob(jobId.toStdString(), targetId.toStdString(), before);
}

void AppBridge::setJobPriority(const QString& jobId, int priority)
{
    if (!m_monitor || jobId.isEmpty()) return;
    m_monitor->setJobPriority(jobId.toStdString(), priority);
}

void AppBridge::deleteJob(const QString& jobId)
{
    if (!m_monitor || jobId.isEmpty()) return;
    m_monitor->deleteJob(jobId.toStdString());
}

void AppBridge::requeueJob(const QString& jobId)
{
    if (!m_monitor || jobId.isEmpty()) return;
    m_monitor->requeueJob(jobId.toStdString());
}

void AppBridge::archiveJob(const QString& jobId)
{
    if (!m_monitor || jobId.isEmpty()) return;
    m_monitor->archiveJob(jobId.toStdString());
}

void AppBridge::retryFailedChunks(const QString& jobId)
{
    if (!m_monitor || jobId.isEmpty()) return;
    m_monitor->retryFailedChunks(jobId.toStdString());
}

void AppBridge::openJobOutput(const QString& jobId)
{
    if (!m_monitor || jobId.isEmpty()) return;

    const std::string id = jobId.toStdString();
    const auto& jobs = m_monitor->cachedJobs();
    auto it = std::find_if(jobs.begin(), jobs.end(),
        [&](const JobInfo& j) { return j.manifest.job_id == id; });
    if (it == jobs.end())
    {
        MonitorLog::instance().warn(
            "AppBridge", "openJobOutput: job not found: " + id);
        return;
    }

    if (!it->manifest.output_dir.has_value()
        || it->manifest.output_dir->empty())
    {
        MonitorLog::instance().warn(
            "AppBridge", "openJobOutput: job has no output_dir: " + id);
        return;
    }

    // Manifests store paths in canonical Windows form. fromCanonicalPath
    // is identity on Windows and translates to /Volumes/... on macOS via
    // the configured path mappings. If no mapping matches, the helper
    // returns a separator-normalized version of the canonical path,
    // which generally won't exist on the local filesystem — log instead
    // of opening a bogus path.
    const std::string& canonical = *it->manifest.output_dir;
    const std::string native = MR::fromCanonicalPath(
        canonical, m_monitor->config().path_mappings);

    std::filesystem::path p(native);
    std::error_code ec;
    if (!std::filesystem::exists(p, ec))
    {
        MonitorLog::instance().warn(
            "AppBridge",
            "openJobOutput: resolved path does not exist locally — "
            "check Path Mappings in Settings. canonical='" + canonical
            + "' resolved='" + native + "'");
        return;
    }

    MR::openFolderInExplorer(p);
}

void AppBridge::requestSubmissionMode()
{
    // The MonitorApp flag was a one-shot consumed by the old ImGui
    // dashboard; with AppBridge driving UI state we use our own
    // submissionMode property directly.
    setSubmissionMode(true);
}

void AppBridge::reassignChunk(qint64 chunkId, const QString& targetNodeId)
{
    if (!m_monitor) return;
    m_monitor->reassignChunk(static_cast<int64_t>(chunkId),
                             targetNodeId.toStdString());
}

QString AppBridge::resubmitChunkAsJob(const QString& jobId,
                                      int frameStart, int frameEnd,
                                      int chunkSize)
{
    if (!m_monitor || jobId.isEmpty())
        return {};
    const std::string slug = m_monitor->resubmitChunkAsJob(
        jobId.toStdString(), frameStart, frameEnd, chunkSize);
    return QString::fromStdString(slug);
}

QVariantMap AppBridge::templateById(const QString& templateId) const
{
    if (!m_monitor) return {};
    const std::string id = templateId.toStdString();
    const auto& tmpls = m_monitor->cachedTemplates();
    auto it = std::find_if(tmpls.begin(), tmpls.end(),
        [&](const JobTemplate& t) { return t.template_id == id; });
    if (it == tmpls.end()) return {};

    const JobTemplate& t = *it;

#ifdef Q_OS_MACOS
    // Templates are typically authored on Windows; their default flag
    // values can contain canonical Windows paths (Z:\..., \\server\...).
    // Translate to native Mac form so the submission form shows
    // /Volumes/... in the placeholder. Non-path defaults (runtime
    // tokens like {frame}, empty strings) pass through unchanged because
    // translatePath only rewrites strings that prefix-match a configured
    // mapping. Submission re-canonicalizes via toCanonicalPath() so the
    // round-trip is loss-free.
    const auto& mappingsForUi = m_monitor->config().path_mappings;
#endif

    QVariantList flags;
    flags.reserve(static_cast<int>(t.flags.size()));
    for (const auto& f : t.flags)
    {
        QVariantMap fm;
        fm["flag"]     = QString::fromStdString(f.flag);
#ifdef Q_OS_MACOS
        fm["value"]    = f.value.has_value()
                         ? QString::fromStdString(
                             MR::fromCanonicalPath(*f.value, mappingsForUi))
                         : QString();
#else
        fm["value"]    = f.value.has_value()
                         ? QString::fromStdString(*f.value) : QString();
#endif
        fm["info"]     = QString::fromStdString(f.info);
        fm["help"]     = QString::fromStdString(f.help);
        fm["editable"] = f.editable;
        fm["required"] = f.required;
        fm["type"]     = QString::fromStdString(f.type);
        fm["filter"]   = QString::fromStdString(f.filter);
        fm["id"]       = QString::fromStdString(f.id);
        flags.push_back(fm);
    }

    QVariantMap m;
    m["templateId"]   = QString::fromStdString(t.template_id);
    m["name"]         = QString::fromStdString(t.name);
    m["frameStart"]   = t.job_defaults.frame_start;
    m["frameEnd"]     = t.job_defaults.frame_end;
    m["chunkSize"]    = t.job_defaults.chunk_size;
    m["priority"]     = t.job_defaults.priority;
    m["maxRetries"]   = t.job_defaults.max_retries;
    m["flags"]        = flags;
    return m;
}

void AppBridge::submitJob(const QString& templateId,
                          const QString& jobName,
                          const QStringList& flagValues,
                          int frameStart, int frameEnd,
                          int chunkSize, int priority)
{
    if (!m_monitor)
    {
        emit submissionFailed(tr("Backend not ready"));
        return;
    }

    const std::string tid = templateId.toStdString();
    const auto& tmpls = m_monitor->cachedTemplates();
    auto it = std::find_if(tmpls.begin(), tmpls.end(),
        [&](const JobTemplate& t) { return t.template_id == tid; });
    if (it == tmpls.end())
    {
        emit submissionFailed(tr("Unknown template: %1").arg(templateId));
        return;
    }
    const JobTemplate& tmpl = *it;

    std::vector<std::string> fvs;
    fvs.reserve(static_cast<size_t>(flagValues.size()));
    for (const auto& v : flagValues)
        fvs.push_back(v.toStdString());

    const std::string os  = MR::getOS();
    const std::string cmd = MR::getCmdForOS(tmpl.cmd, os);

    // Validate using existing TemplateManager helper — same checks the
    // ImGui submission panel ran.
    auto errs = TemplateManager::validateSubmission(
        tmpl, fvs, cmd, jobName.toStdString(),
        frameStart, frameEnd, chunkSize,
        m_monitor->farmPath() / "jobs");
    if (!errs.empty())
    {
        emit submissionFailed(QString::fromStdString(errs.front()));
        return;
    }

    const std::string slug = TemplateManager::generateSlug(
        jobName.toStdString(), m_monitor->farmPath() / "jobs");
    if (slug.empty())
    {
        emit submissionFailed(tr("Failed to generate job slug"));
        return;
    }

    JobManifest manifest = TemplateManager::bakeManifestStatic(
        tmpl, fvs, cmd, slug,
        frameStart, frameEnd, chunkSize,
        tmpl.job_defaults.max_retries,
        tmpl.job_defaults.timeout_seconds,
        m_monitor->identity().nodeId(), os);

#ifdef Q_OS_MACOS
    // MinRender is Windows-first: paths in manifests, SQLite, and on the
    // wire are stored in canonical Windows form. On macOS we translate at
    // the submission boundary so a Mac artist picking /Volumes/... lands
    // in the database as Z:\... — exactly what a Windows submitter would
    // produce. After this step submitted_os is overwritten to "windows"
    // because the manifest's paths are now in Windows form; the dispatcher
    // (dispatch_manager.cpp:410) keys on submitted_os to decide whether
    // to run cross-OS translation, and getting that wrong double-translates.
    if (MR::currentOsTag() == "mac")
    {
        const auto& mappings = m_monitor->config().path_mappings;
        for (auto& flag : manifest.flags)
        {
            if (flag.value.has_value() && !flag.value->empty())
                flag.value = MR::toCanonicalPath(*flag.value, mappings);
        }
        if (manifest.output_dir.has_value() && !manifest.output_dir->empty())
            manifest.output_dir = MR::toCanonicalPath(*manifest.output_dir, mappings);
        manifest.submitted_os = "windows";
    }
#endif

    if (m_monitor->isLeader())
    {
        SubmitRequest sr;
        sr.manifest = std::move(manifest);
        sr.priority = priority;
        m_monitor->dispatchManager().queueSubmission(std::move(sr));
        emit submissionSucceeded(QString::fromStdString(slug));
        return;
    }

    // Worker — POST to leader. Marshal the result back to the UI thread
    // since postToLeaderAsync's callback runs on the http worker.
    nlohmann::json body;
    body["manifest"] = manifest;
    body["priority"] = priority;

    const QString slugQ = QString::fromStdString(slug);
    m_monitor->postToLeaderAsync(
        "/api/jobs", body.dump(),
        [this, slugQ](bool ok, const std::string& response) {
            const QString resp = QString::fromStdString(response);
            QMetaObject::invokeMethod(
                this,
                [this, ok, slugQ, resp]() {
                    if (ok) emit submissionSucceeded(slugQ);
                    else    emit submissionFailed(
                                resp.isEmpty() ? tr("Leader did not respond") : resp);
                },
                Qt::QueuedConnection);
        });
}

bool AppBridge::previewSupported() const
{
#ifdef MINRENDER_HAVE_PREVIEW
    return true;
#else
    return false;
#endif
}

QVariantMap AppBridge::previewInfoForRange(const QString& jobId,
                                           int frameStart, int frameEnd)
{
    QVariantMap out;
    out["found"] = false;
    if (!m_monitor || jobId.isEmpty())
        return out;

    // Locate the job + its output pattern in the cached list.
    const JobInfo* job = nullptr;
    for (const auto& info : m_monitor->cachedJobs())
    {
        if (info.manifest.job_id == jobId.toStdString())
        {
            job = &info;
            break;
        }
    }
    if (!job || !job->manifest.output_dir.has_value()
        || job->manifest.output_dir->empty())
        return out;

    const auto& mappings = m_monitor->config().path_mappings;
    const std::string nativeDir =
        MR::fromCanonicalPath(*job->manifest.output_dir, mappings);

    std::error_code ec;
    if (!std::filesystem::is_directory(nativeDir, ec))
        return out;

    // Filename filter from the output flag's pattern: escape it as a
    // regex, then let each `#` run match digits. DCCs replace the
    // padding themselves (Blender ####, AE [####] — brackets are
    // escaped literally so they simply don't match and we fall back).
    std::string stemRegex;
    for (const auto& f : job->manifest.flags)
    {
        if (!f.is_output || !f.value.has_value() || f.value->empty())
            continue;
        const std::string& v = *f.value;
        const size_t sep = v.find_last_of("/\\");
        const std::string base = sep == std::string::npos ? v : v.substr(sep + 1);
        std::string rx;
        bool inPad = false;
        for (char c : base)
        {
            if (c == '#')
            {
                if (!inPad) { rx += "(\\d+)"; inPad = true; }
                continue;
            }
            inPad = false;
            static const std::string special = R"(\.^$|()[]{}*+?)";
            if (special.find(c) != std::string::npos) rx += '\\';
            rx += c;
        }
        stemRegex = rx;
        break;
    }

    // Sweep the directory: prefer the highest frame number matched by
    // the pattern; fall back to newest mtime among image files.
    // EXR/PNG/JPEG/TIFF decode through the vendored QCView loaders;
    // the rest go through Qt's own image readers (same fallback QCView
    // uses), so the set matches QCView's still-image coverage.
    static const std::set<std::string> kImageExts =
        {".exr", ".png", ".jpg", ".jpeg", ".tif", ".tiff",
         ".bmp", ".webp", ".gif", ".tga"};

    std::optional<std::regex> pattern;
    if (!stemRegex.empty() && stemRegex.find("(\\d+)") != std::string::npos)
    {
        try { pattern = std::regex(stemRegex, std::regex::icase); }
        catch (...) {}
    }

    // A restricted range (pin) is only resolvable through the pattern —
    // the mtime fallback can't attribute a frame number to a file.
    const bool ranged = frameStart >= 0;
    if (ranged && !pattern)
        return out;

    std::string bestFile;
    long long bestFrame = -1;
    std::filesystem::file_time_type bestTime{};

    for (const auto& entry : std::filesystem::directory_iterator(nativeDir, ec))
    {
        if (!entry.is_regular_file(ec))
            continue;
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (!kImageExts.count(ext))
            continue;

        const std::string name = entry.path().filename().string();
        if (pattern)
        {
            std::smatch m;
            if (std::regex_match(name, m, *pattern) && m.size() > 1)
            {
                const long long frame = std::atoll(m[1].str().c_str());
                if (ranged && (frame < frameStart || frame > frameEnd))
                    continue;
                if (frame > bestFrame)
                {
                    bestFrame = frame;
                    bestFile = entry.path().string();
                }
                continue;
            }
            // Pattern exists but this file doesn't match — only use it
            // for the mtime fallback if nothing matched at all.
        }
        if (!ranged && bestFrame < 0)
        {
            const auto t = entry.last_write_time(ec);
            if (bestFile.empty() || t > bestTime)
            {
                bestTime = t;
                bestFile = entry.path().string();
            }
        }
    }

    if (bestFile.empty())
        return out;

    std::string ext = std::filesystem::path(bestFile).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    out["found"] = true;
    out["file"]  = QString::fromStdString(bestFile);
    out["frame"] = bestFrame >= 0 ? QVariant::fromValue(bestFrame) : QVariant();
    out["isExr"] = (ext == ".exr");
    return out;
}

QVariantMap AppBridge::jobPreviewInfo(const QString& jobId)
{
    return previewInfoForRange(jobId, -1, -1);
}

QVariantMap AppBridge::rangePreviewInfo(const QString& jobId,
                                        int frameStart, int frameEnd)
{
    return previewInfoForRange(jobId, frameStart, frameEnd);
}

bool AppBridge::pinPreview(int frameStart, int frameEnd)
{
    if (m_currentJobId.isEmpty() || frameStart < 0 || frameEnd < frameStart)
        return false;

    // The file on disk is the ground truth for "rendered AND copied":
    // staged-but-uncopied frames aren't in the output dir yet, so the
    // pin is refused and the preview keeps following latest.
    const QVariantMap info =
        previewInfoForRange(m_currentJobId, frameStart, frameEnd);
    if (!info.value("found").toBool())
        return false;

    if (m_previewPinStart != frameStart || m_previewPinEnd != frameEnd)
    {
        m_previewPinStart = frameStart;
        m_previewPinEnd   = frameEnd;
        emit previewPinChanged();
    }
    else
    {
        // Re-pinning the same range still restarts the auto-return
        // timer on the QML side.
        emit previewPinChanged();
    }
    return true;
}

void AppBridge::clearPreviewPin()
{
    if (m_previewPinStart < 0 && m_previewPinEnd < 0)
        return;
    m_previewPinStart = -1;
    m_previewPinEnd   = -1;
    emit previewPinChanged();
}

QStringList AppBridge::exrLayers(const QString& filePath)
{
    QStringList out;
#ifdef MINRENDER_HAVE_PREVIEW
    if (filePath.isEmpty())
        return out;
    const auto layers = qcv::EXRImageLoader::discoverLayers(filePath.toStdString());
    out.reserve(static_cast<int>(layers.size()));
    for (const auto& l : layers)
        out.push_back(QString::fromStdString(l));
#else
    Q_UNUSED(filePath)
#endif
    return out;
}

bool AppBridge::openJobEditor(const QString& jobId)
{
    if (!m_monitor || jobId.isEmpty())
        return false;

    const std::string manifestJson =
        m_monitor->getJobManifestJson(jobId.toStdString());
    if (manifestJson.empty())
    {
        MonitorLog::instance().warn("job",
            "openJobEditor: no manifest for " + jobId.toStdString());
        return false;
    }

    JobManifest manifest;
    try
    {
        manifest = nlohmann::json::parse(manifestJson).get<JobManifest>();
    }
    catch (const std::exception& e)
    {
        MonitorLog::instance().warn("job",
            std::string("openJobEditor: bad manifest: ") + e.what());
        return false;
    }

#ifdef Q_OS_MACOS
    // Same display translation as templateById: manifests store
    // canonical Windows paths; show native ones, re-canonicalize on
    // apply.
    const auto& mappingsForUi = m_monitor->config().path_mappings;
    auto display = [&](const std::string& v) {
        return QString::fromStdString(MR::fromCanonicalPath(v, mappingsForUi));
    };
#else
    auto display = [](const std::string& v) {
        return QString::fromStdString(v);
    };
#endif

    const auto& tmpls = m_monitor->cachedTemplates();
    auto it = std::find_if(tmpls.begin(), tmpls.end(),
        [&](const JobTemplate& t) { return t.template_id == manifest.template_id; });
    const bool templateFound = (it != tmpls.end() && it->valid);

    QVariantList flags;
    if (templateFound)
    {
        // Overlay manifest values onto the template's flags. Baking may
        // have dropped empty optional positionals, so match by flag
        // string and consume the manifest's entries per name in order —
        // a flag with no manifest entry was empty at submit time.
        std::map<std::string, std::deque<std::string>> valuesByFlag;
        for (const auto& mf : manifest.flags)
            valuesByFlag[mf.flag].push_back(mf.value.value_or(""));

        for (const auto& tf : it->flags)
        {
            QVariantMap fm;
            fm["flag"]     = QString::fromStdString(tf.flag);
            fm["info"]     = QString::fromStdString(tf.info);
            fm["help"]     = QString::fromStdString(tf.help);
            fm["editable"] = tf.editable;
            fm["required"] = tf.required;
            fm["type"]     = QString::fromStdString(tf.type);
            fm["filter"]   = QString::fromStdString(tf.filter);
            fm["id"]       = QString::fromStdString(tf.id);

            auto& dq = valuesByFlag[tf.flag];
            if (!dq.empty())
            {
                fm["value"] = display(dq.front());
                dq.pop_front();
            }
            else
            {
                fm["value"] = QString();
            }
            flags.push_back(fm);
        }
    }
    else
    {
        // Template gone (deleted or renamed): edit the manifest's flags
        // raw, 1:1 with manifest.flags order — applyJobEdit's mutate
        // path relies on that alignment.
        for (const auto& mf : manifest.flags)
        {
            QVariantMap fm;
            fm["flag"]     = QString::fromStdString(mf.flag);
            fm["value"]    = display(mf.value.value_or(""));
            fm["info"]     = mf.flag.empty() ? tr("(positional)")
                                             : QString::fromStdString(mf.flag);
            fm["help"]     = QString();
            fm["editable"] = true;
            fm["required"] = false;
            fm["type"]     = QString();
            fm["filter"]   = QString();
            fm["id"]       = QString();
            flags.push_back(fm);
        }
    }

    int priority = 50;
    for (const auto& info : m_monitor->cachedJobs())
    {
        if (info.manifest.job_id == jobId.toStdString())
        {
            priority = info.current_priority;
            break;
        }
    }

    QVariantMap seed;
    seed["jobId"]          = jobId;
    seed["templateId"]     = QString::fromStdString(manifest.template_id);
    seed["templateName"]   = templateFound
                             ? QString::fromStdString(it->name)
                             : QString::fromStdString(manifest.template_id);
    seed["templateFound"]  = templateFound;
    seed["flags"]          = flags;
    seed["frameStart"]     = manifest.frame_start;
    seed["frameEnd"]       = manifest.frame_end;
    seed["chunkSize"]      = manifest.chunk_size;
    seed["priority"]       = priority;
    seed["maxRetries"]     = manifest.max_retries;
    seed["timeoutSeconds"] = manifest.timeout_seconds.value_or(0);

    m_editJobId = jobId;
    m_editSeed  = seed;
    emit editJobChanged();
    return true;
}

void AppBridge::closeJobEditor()
{
    if (m_editJobId.isEmpty() && m_editSeed.isEmpty())
        return;
    m_editJobId.clear();
    m_editSeed.clear();
    emit editJobChanged();
}

void AppBridge::applyJobEdit(const QStringList& flagValues,
                             int frameStart, int frameEnd,
                             int chunkSize, int priority,
                             int maxRetries, int timeoutSeconds,
                             const QString& mode)
{
    if (!m_monitor || m_editJobId.isEmpty())
    {
        emit submissionFailed(tr("No job is being edited"));
        return;
    }
    if (frameStart > frameEnd)
    {
        emit submissionFailed(tr("Frame start must be <= frame end"));
        return;
    }
    if (chunkSize < 1)
    {
        emit submissionFailed(tr("Chunk size must be at least 1"));
        return;
    }

    const std::string jobId = m_editJobId.toStdString();

    // Re-fetch the stored manifest rather than trusting the seed — the
    // job may have been edited elsewhere since the dialog opened.
    const std::string storedJson = m_monitor->getJobManifestJson(jobId);
    if (storedJson.empty())
    {
        emit submissionFailed(tr("Job manifest unavailable (leader unreachable?)"));
        return;
    }
    JobManifest manifest;
    try
    {
        manifest = nlohmann::json::parse(storedJson).get<JobManifest>();
    }
    catch (const std::exception& e)
    {
        emit submissionFailed(tr("Stored manifest is invalid: %1").arg(e.what()));
        return;
    }

    std::vector<std::string> fvs;
    fvs.reserve(static_cast<size_t>(flagValues.size()));
    for (const auto& v : flagValues)
        fvs.push_back(v.toStdString());

    const bool templateFound = m_editSeed.value("templateFound").toBool();
    if (templateFound)
    {
        // Re-bake through the template, exactly like a fresh submission,
        // then restore the job's identity/provenance fields.
        const std::string tid = m_editSeed.value("templateId").toString().toStdString();
        const auto& tmpls = m_monitor->cachedTemplates();
        auto it = std::find_if(tmpls.begin(), tmpls.end(),
            [&](const JobTemplate& t) { return t.template_id == tid; });
        if (it == tmpls.end())
        {
            emit submissionFailed(tr("Template no longer available: %1")
                .arg(QString::fromStdString(tid)));
            return;
        }
        const JobTemplate& tmpl = *it;

        if (fvs.size() != tmpl.flags.size())
        {
            emit submissionFailed(tr("Template changed while editing — reopen the editor"));
            return;
        }
        for (size_t i = 0; i < tmpl.flags.size(); ++i)
        {
            if (tmpl.flags[i].required && tmpl.flags[i].editable && fvs[i].empty())
            {
                emit submissionFailed(tr("Required flag is empty: %1")
                    .arg(QString::fromStdString(
                        tmpl.flags[i].info.empty() ? tmpl.flags[i].flag
                                                   : tmpl.flags[i].info)));
                return;
            }
        }

        const std::string os  = MR::getOS();
        const std::string cmd = MR::getCmdForOS(tmpl.cmd, os);
        if (cmd.empty())
        {
            emit submissionFailed(tr("Template has no executable for this OS"));
            return;
        }

        JobManifest baked = TemplateManager::bakeManifestStatic(
            tmpl, fvs, cmd, jobId,
            frameStart, frameEnd, chunkSize,
            maxRetries,
            timeoutSeconds > 0 ? std::optional<int>(timeoutSeconds) : std::nullopt,
            m_monitor->identity().nodeId(), os);

        baked.submitted_by    = manifest.submitted_by;
        baked.submitted_at_ms = manifest.submitted_at_ms;
        baked.submitted_os    = manifest.submitted_os;
        manifest = std::move(baked);
    }
    else
    {
        // Mutate the stored manifest in place — the edit form's rows
        // were built 1:1 from manifest.flags.
        if (fvs.size() != manifest.flags.size())
        {
            emit submissionFailed(tr("Job changed while editing — reopen the editor"));
            return;
        }
        for (size_t i = 0; i < fvs.size(); ++i)
            manifest.flags[i].value = fvs[i];

        manifest.frame_start = frameStart;
        manifest.frame_end   = frameEnd;
        manifest.chunk_size  = chunkSize;
        manifest.max_retries = maxRetries;
        manifest.timeout_seconds =
            timeoutSeconds > 0 ? std::optional<int>(timeoutSeconds) : std::nullopt;

        // Re-derive output_dir from the first output flag. Manual
        // separator scan instead of fs::path so canonical Windows paths
        // parse on macOS too; keep the old value if nothing derivable.
        for (const auto& f : manifest.flags)
        {
            if (!f.is_output || !f.value.has_value() || f.value->empty())
                continue;
            const std::string& v = *f.value;
            const size_t sep = v.find_last_of("/\\");
            if (sep != std::string::npos && sep > 0)
                manifest.output_dir = v.substr(0, sep);
            break;
        }
    }

#ifdef Q_OS_MACOS
    // Same canonicalization boundary as submitJob — see the comment
    // there. Native mac paths go back to canonical Windows form.
    if (MR::currentOsTag() == "mac")
    {
        const auto& mappings = m_monitor->config().path_mappings;
        for (auto& flag : manifest.flags)
        {
            if (flag.value.has_value() && !flag.value->empty())
                flag.value = MR::toCanonicalPath(*flag.value, mappings);
        }
        if (manifest.output_dir.has_value() && !manifest.output_dir->empty())
            manifest.output_dir = MR::toCanonicalPath(*manifest.output_dir, mappings);
        manifest.submitted_os = "windows";
    }
#endif

    manifest.job_id = jobId;

    nlohmann::json mj = manifest;
    m_monitor->editJob(jobId, mj.dump(), mode.toStdString(), priority);

    emit editApplied();
    closeJobEditor();
}

void AppBridge::requestRestart()
{
    if (!m_monitor) return;
    m_monitor->launchRestartSidecar();
}

QString AppBridge::urlToLocalPath(const QUrl& url) const
{
    return url.toLocalFile();
}

bool AppBridge::syncRootIsValid() const
{
    if (!m_monitor) return false;
    const std::string& p = m_monitor->config().sync_root;
    if (p.empty()) return false;
    std::error_code ec;
    return std::filesystem::is_directory(p, ec);
}

} // namespace MR

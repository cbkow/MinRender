#include "ui/app_bridge.h"

#include "core/config.h"
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

#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <algorithm>
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

void AppBridge::setCurrentJobId(const QString& jobId)
{
    if (m_currentJobId == jobId)
        return;
    m_currentJobId = jobId;
    emit currentJobIdChanged();

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
}

void AppBridge::refreshChunks()
{
    if (!m_monitor || m_currentJobId.isEmpty())
        return;
    // On workers this is a blocking HTTP GET to the leader (~1.5 s
    // worst case). Phase 5's frame grid may promote this to the async
    // postToLeaderAsync path if the UI stalls become noticeable.
    m_chunksModel->setChunks(
        m_monitor->getChunksForJob(m_currentJobId.toStdString()));
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

    // MonitorApp::refreshCachedJobs runs on the same thread (the 50 ms
    // QTimer that calls this method), so direct push is safe. The model
    // diffs internally and only emits dataChanged for changed rows.
    m_jobsModel->setJobs(m_monitor->cachedJobs());

    // PeerManager::getPeerSnapshot takes a mutex under the hood so it's
    // safe from any thread; we call it from the UI thread anyway.
    m_nodesModel->setPeers(m_monitor->peerManager().getPeerSnapshot());

    // Template cache is refreshed alongside jobs (every 2 s in
    // MonitorApp::update). TemplatesModel short-circuits when the set
    // is equivalent, so polling here at 20 Hz is essentially free.
    m_templatesModel->setTemplates(m_monitor->cachedTemplates());
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
    m_monitor->saveConfig();
    takeSnapshot();
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

void AppBridge::requestSubmissionMode()
{
    if (!m_monitor) return;
    m_monitor->requestSubmissionMode();
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

    QVariantList flags;
    flags.reserve(static_cast<int>(t.flags.size()));
    for (const auto& f : t.flags)
    {
        QVariantMap fm;
        fm["flag"]     = QString::fromStdString(f.flag);
        fm["value"]    = f.value.has_value()
                         ? QString::fromStdString(*f.value) : QString();
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

void AppBridge::requestRestart()
{
    if (!m_monitor) return;
    m_monitor->launchRestartSidecar();
}

} // namespace MR

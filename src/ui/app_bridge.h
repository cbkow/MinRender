#pragma once

#include "core/config.h"
#include "ui/models/chunks_model.h"
#include "ui/models/jobs_model.h"
#include "ui/models/log_model.h"
#include "ui/models/nodes_model.h"
#include "ui/models/templates_model.h"

#include <QColor>
#include <QObject>
#include <QString>
#include <QTimer>

#include <memory>

namespace MR {

class MonitorApp;

// Qt-facing facade for MonitorApp. Lives on the main thread, owned by
// main_qt.cpp, exposed to QML via setContextProperty("appBridge", ...).
// Read properties reflect MonitorApp state; write properties mutate
// MonitorApp::config() in-memory. Changes don't touch disk until
// saveSettings() is invoked explicitly (no auto-save — matches the plan).
class AppBridge : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool    farmRunning        READ farmRunning        NOTIFY farmRunningChanged)
    Q_PROPERTY(QColor  accentColor        READ accentColor        NOTIFY accentColorChanged)

    Q_PROPERTY(MR::JobsModel*      jobsModel      READ jobsModel      CONSTANT)
    Q_PROPERTY(MR::NodesModel*     nodesModel     READ nodesModel     CONSTANT)
    Q_PROPERTY(MR::LogModel*       logModel       READ logModel       CONSTANT)
    Q_PROPERTY(MR::ChunksModel*    chunksModel    READ chunksModel    CONSTANT)
    Q_PROPERTY(MR::TemplatesModel* templatesModel READ templatesModel CONSTANT)

    Q_PROPERTY(QString currentJobId READ currentJobId WRITE setCurrentJobId
               NOTIFY currentJobIdChanged)

    // "This Node" descriptors — stable for the life of the process except
    // for isLeader and nodeState, which flip with leadership / tray toggles.
    Q_PROPERTY(QString thisNodeId       READ thisNodeId       CONSTANT)
    Q_PROPERTY(QString thisNodeHostname READ thisNodeHostname CONSTANT)
    Q_PROPERTY(QString thisNodeGpu      READ thisNodeGpu      CONSTANT)
    Q_PROPERTY(int     thisNodeCpuCores READ thisNodeCpuCores CONSTANT)
    Q_PROPERTY(qint64  thisNodeRamMb    READ thisNodeRamMb    CONSTANT)
    Q_PROPERTY(bool    thisNodeIsLeader READ thisNodeIsLeader NOTIFY thisNodeIsLeaderChanged)
    Q_PROPERTY(bool    thisNodeActive   READ thisNodeActive   NOTIFY thisNodeActiveChanged)

    Q_PROPERTY(QString syncRoot           READ syncRoot           WRITE setSyncRoot           NOTIFY syncRootChanged)
    Q_PROPERTY(QString tagsCsv            READ tagsCsv            WRITE setTagsCsv            NOTIFY tagsCsvChanged)
    Q_PROPERTY(int     httpPort           READ httpPort           WRITE setHttpPort           NOTIFY httpPortChanged)
    Q_PROPERTY(QString ipOverride         READ ipOverride         WRITE setIpOverride         NOTIFY ipOverrideChanged)
    Q_PROPERTY(bool    udpEnabled         READ udpEnabled         WRITE setUdpEnabled         NOTIFY udpEnabledChanged)
    Q_PROPERTY(int     udpPort            READ udpPort            WRITE setUdpPort            NOTIFY udpPortChanged)
    Q_PROPERTY(bool    showNotifications  READ showNotifications  WRITE setShowNotifications  NOTIFY showNotificationsChanged)
    Q_PROPERTY(bool    stagingEnabled     READ stagingEnabled     WRITE setStagingEnabled     NOTIFY stagingEnabledChanged)

public:
    explicit AppBridge(MonitorApp* monitor, QObject* parent = nullptr);
    ~AppBridge() override;

    bool farmRunning() const;
    QColor accentColor() const { return m_accentColor; }

    JobsModel*      jobsModel()      const { return m_jobsModel.get(); }
    NodesModel*     nodesModel()     const { return m_nodesModel.get(); }
    LogModel*       logModel()       const { return m_logModel.get(); }
    ChunksModel*    chunksModel()    const { return m_chunksModel.get(); }
    TemplatesModel* templatesModel() const { return m_templatesModel.get(); }

    QString currentJobId() const { return m_currentJobId; }
    void setCurrentJobId(const QString& jobId);

    QString thisNodeId() const;
    QString thisNodeHostname() const;
    QString thisNodeGpu() const;
    int     thisNodeCpuCores() const;
    qint64  thisNodeRamMb() const;
    bool    thisNodeIsLeader() const;
    bool    thisNodeActive() const;

    Q_INVOKABLE void toggleNodeActive();
    Q_INVOKABLE void unsuspendNode(const QString& nodeId);

    QString syncRoot() const;
    void setSyncRoot(const QString& v);

    QString tagsCsv() const;
    void setTagsCsv(const QString& v);

    int httpPort() const;
    void setHttpPort(int v);

    QString ipOverride() const;
    void setIpOverride(const QString& v);

    bool udpEnabled() const;
    void setUdpEnabled(bool v);

    int udpPort() const;
    void setUdpPort(int v);

    bool showNotifications() const;
    void setShowNotifications(bool v);

    bool stagingEnabled() const;
    void setStagingEnabled(bool v);

    // Polled from main_qt.cpp's 50 ms tick. Emits *Changed signals only
    // when the underlying MonitorApp state actually shifted.
    void refresh();

    // Revert all in-memory edits back to what's currently on disk. Used
    // by the Settings panel's Cancel button; a no-op after saveSettings.
    Q_INVOKABLE void revertSettings();

    Q_INVOKABLE void saveSettings();
    Q_INVOKABLE void requestRestart();

signals:
    void farmRunningChanged();
    void accentColorChanged();
    void syncRootChanged();
    void tagsCsvChanged();
    void httpPortChanged();
    void ipOverrideChanged();
    void udpEnabledChanged();
    void udpPortChanged();
    void showNotificationsChanged();
    void stagingEnabledChanged();
    void currentJobIdChanged();
    void thisNodeIsLeaderChanged();
    void thisNodeActiveChanged();

private:
    // Snapshot of MonitorApp::config() taken at construction and after each
    // saveSettings(). revertSettings() copies this back, emitting *Changed
    // for every property so bound QML rebinds.
    void takeSnapshot();
    void emitAllSettingsChanged();

    MonitorApp* m_monitor;
    QColor m_accentColor;
    Config m_snapshot;
    // Drives the 3 s ChunksModel refresh while m_currentJobId is set.
    void refreshChunks();

    std::unique_ptr<JobsModel>      m_jobsModel;
    std::unique_ptr<NodesModel>     m_nodesModel;
    std::unique_ptr<LogModel>       m_logModel;
    std::unique_ptr<ChunksModel>    m_chunksModel;
    std::unique_ptr<TemplatesModel> m_templatesModel;
    QTimer                          m_chunksTimer;
    QString                         m_currentJobId;
    bool m_lastFarmRunning  = false;
    bool m_lastIsLeader     = false;
    bool m_lastNodeActive   = true;
};

} // namespace MR

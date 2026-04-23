#pragma once

#include "core/config.h"
#include "ui/models/jobs_model.h"

#include <QColor>
#include <QObject>
#include <QString>

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

    Q_PROPERTY(MR::JobsModel* jobsModel READ jobsModel CONSTANT)

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

    JobsModel* jobsModel() const { return m_jobsModel.get(); }

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

private:
    // Snapshot of MonitorApp::config() taken at construction and after each
    // saveSettings(). revertSettings() copies this back, emitting *Changed
    // for every property so bound QML rebinds.
    void takeSnapshot();
    void emitAllSettingsChanged();

    MonitorApp* m_monitor;
    QColor m_accentColor;
    Config m_snapshot;
    std::unique_ptr<JobsModel> m_jobsModel;
    bool m_lastFarmRunning = false;
};

} // namespace MR

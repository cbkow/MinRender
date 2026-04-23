#include "ui/app_bridge.h"

#include "core/config.h"
#include "monitor/monitor_app.h"
#include "ui/platform/accent_color.h"

#include <QStringList>

#include <algorithm>

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
    , m_lastFarmRunning(monitor ? monitor->isFarmRunning() : false)
{
    takeSnapshot();
}

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
    emit fontScaleChanged();
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

qreal AppBridge::fontScale() const
{
    return m_monitor ? static_cast<qreal>(m_monitor->config().font_scale) : 1.0;
}

void AppBridge::setFontScale(qreal v)
{
    if (!m_monitor) return;
    const auto f = static_cast<float>(std::clamp(v, 0.5, 2.0));
    if (m_monitor->config().font_scale == f) return;
    m_monitor->config().font_scale = f;
    emit fontScaleChanged();
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

void AppBridge::requestRestart()
{
    if (!m_monitor) return;
    m_monitor->launchRestartSidecar();
}

} // namespace MR

#include "ui/platform/tray.h"

#include <QAction>
#include <QCoreApplication>
#include <QDir>
#include <QIcon>
#include <QMenu>
#include <QObject>
#include <QString>
#include <QSystemTrayIcon>

namespace MR {

namespace {

QIcon loadTrayIcon()
{
    // Embedded first (always present post-link); fall back to the copy
    // POST_BUILD places next to the binary in dev builds.
    QIcon icon(QStringLiteral(":/icons/minrender.ico"));
    if (icon.isNull())
    {
        icon = QIcon(QCoreApplication::applicationDirPath()
                     + QStringLiteral("/resources/icons/minrender.ico"));
    }
    return icon;
}

} // namespace

Tray::Tray() = default;

Tray::~Tray()
{
    shutdown();
}

bool Tray::init()
{
    if (m_initialized)
        return true;

    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return false;

    m_menu = std::make_unique<QMenu>();
    m_showAction = m_menu->addAction(QObject::tr("Show Window"));
    m_stopResumeAction = m_menu->addAction(QObject::tr("Stop"));
    m_menu->addSeparator();
    m_exitAction = m_menu->addAction(QObject::tr("Exit"));

    QObject::connect(m_showAction, &QAction::triggered,
        [this]() { if (onShowWindow) onShowWindow(); });
    QObject::connect(m_stopResumeAction, &QAction::triggered,
        [this]() { if (onStopResume) onStopResume(); });
    QObject::connect(m_exitAction, &QAction::triggered,
        [this]() { if (onExit) onExit(); });

    m_icon = std::make_unique<QSystemTrayIcon>(loadTrayIcon());
    m_icon->setContextMenu(m_menu.get());
    m_icon->setToolTip(QStringLiteral("MinRender"));

    // Single-click / double-click both restore the window, matching the
    // Win32 tray's behavior.
    QObject::connect(m_icon.get(), &QSystemTrayIcon::activated,
        [this](QSystemTrayIcon::ActivationReason reason) {
            if (reason == QSystemTrayIcon::Trigger ||
                reason == QSystemTrayIcon::DoubleClick)
            {
                if (onShowWindow) onShowWindow();
            }
        });

    m_icon->show();
    m_initialized = true;
    updateMenuLabels();
    return true;
}

void Tray::shutdown()
{
    if (m_icon)
    {
        m_icon->hide();
        m_icon.reset();
    }
    m_menu.reset();
    m_showAction = nullptr;
    m_stopResumeAction = nullptr;
    m_exitAction = nullptr;
    m_initialized = false;
}

void Tray::setTooltip(const std::string& text)
{
    if (m_icon)
        m_icon->setToolTip(QString::fromStdString(text));
}

void Tray::setStatusText(const std::string& text)
{
    if (m_statusText == text)
        return;
    m_statusText = text;
    updateMenuLabels();
}

void Tray::setNodeActive(bool active)
{
    if (m_initialized && m_nodeActive == active)
        return;
    m_nodeActive = active;
    updateMenuLabels();
    // Phase 6 will swap in mR_tray_GREEN / mR_tray_GREY / mR_tray_RED
    // icons here to reflect node state.
}

void Tray::updateMenuLabels()
{
    if (m_stopResumeAction)
    {
        m_stopResumeAction->setText(m_nodeActive
            ? QObject::tr("Stop")
            : QObject::tr("Resume"));
    }
}

} // namespace MR

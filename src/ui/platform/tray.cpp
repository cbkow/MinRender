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
#ifdef Q_OS_MACOS
    // macOS menu bar status items expect a template image — pure black
    // on transparent — that the system auto-tints based on the
    // currently-active appearance (light / dark menu bar) and selection
    // state. setIsMask flips the underlying NSImage's `template`
    // property to YES so the menu bar does the tinting for us.
    QIcon icon(QStringLiteral(":/icons/MinRenderBlack.png"));
    icon.setIsMask(true);
    return icon;
#else
    // Windows / Linux trays render the icon as-supplied; use the
    // multi-resolution colour .ico so the cobalt mark reads at every
    // tray size from 16px up.
    return QIcon(QStringLiteral(":/icons/minrender.ico"));
#endif
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
    // Status-coloured tray icon is deferred. The new monochrome logo is
    // a single-shape silhouette; when we want stop/active/error tints,
    // do them at runtime by painting Theme.success/warn/error onto a
    // QPixmap copy of the loaded icon — avoids shipping five PNGs.
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

// Qt 6 Quick GUI entry point.
//
// Linked only into the `minrender` target. The `minrender-headless` target
// uses main_headless.cpp instead.
//
// Phase 2 scope: MonitorApp comes up alongside the Qt shell and drives a
// 50 ms QTimer pump (matches the headless loop). AppBridge + settings
// land in the next commit. See docs/qt-port-plan.md.

#include "core/single_instance.h"
#include "monitor/monitor_app.h"
#include "ui/app_bridge.h"
#include "ui/painters/frame_grid.h"
#include "ui/platform/color_space.h"
#include "ui/platform/title_bar.h"
#include "ui/platform/tray.h"
#include "ui/updater/app_updater.h"

#include <QtQml/qqml.h>

#include <QApplication>
#include <QColorSpace>
#include <QDir>
#include <QFontDatabase>
#include <QIcon>
#include <QLoggingCategory>
#include <QPalette>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlError>
#include <QQuickStyle>
#include <QSurfaceFormat>
#include <QTimer>
#include <QWindow>

#include <iostream>

#ifdef _MSC_VER
#include <crtdbg.h>
#include <cstdlib>

static void invalidParameterHandler(
    const wchar_t* expression, const wchar_t* function,
    const wchar_t* file, unsigned int line, uintptr_t /*reserved*/)
{
    std::wcerr << L"[CRT] Invalid parameter: " << (expression ? expression : L"(null)")
               << L" in " << (function ? function : L"(null)")
               << L" at " << (file ? file : L"(null)")
               << L":" << line << std::endl;
}
#endif

int main(int argc, char* argv[])
{
#ifdef _MSC_VER
    _set_invalid_parameter_handler(invalidParameterHandler);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_DEBUG);
#endif

    // Pin the rendering surface to sRGB before any QWindow / QQuickWindow
    // is created. On macOS the RHI Metal backend reads QSurfaceFormat to
    // decide the CAMetalLayer's colorspace; with the default unset it
    // inherits the screen's gamut (Display P3 on modern Macs), so hex
    // tokens like Theme.qml's #2E5BFF render visibly oversaturated. The
    // NSWindow.colorSpace tag in pinSRgbColorSpace() only labels the
    // window for screenshots — it does not retag the Metal layer Qt
    // already created. Setting the default format here flows into the
    // RHI for every QQuickWindow.
    //
    // Windows / Linux: setting sRGB here is a no-op in practice — D3D11
    // and X11/Wayland surfaces already render in sRGB.
    {
        QSurfaceFormat fmt = QSurfaceFormat::defaultFormat();
        fmt.setColorSpace(QColorSpace(QColorSpace::SRgb));
        QSurfaceFormat::setDefaultFormat(fmt);
    }

    // QApplication (not QGuiApplication) for QSystemTrayIcon.
    QApplication app(argc, argv);
    QApplication::setOrganizationName("MinRender");
    QApplication::setApplicationName("MinRender");
    QApplication::setApplicationVersion(APP_VERSION);

    // Register C++-backed QML types. FrameGrid is a QQuickPaintedItem
    // subclass and has to be registered before the QML engine loads
    // any module that references it.
    qmlRegisterType<MR::FrameGrid>("MinRenderUi", 1, 0, "FrameGrid");

    MR::SingleInstance singleInstance("MinRenderMonitor");
    if (!singleInstance.isFirst())
    {
        singleInstance.signalExisting();
        return 0;
    }

    // Backend online before the QML engine — future AppBridge bindings
    // expect a live MonitorApp, so QML's first paint can read real state.
    MR::MonitorApp monitor;
    if (!monitor.init())
    {
        std::cerr << "Failed to initialize MonitorApp" << std::endl;
        return 1;
    }

    QQuickStyle::setStyle("Fusion");
#ifndef Q_OS_MACOS
    // On Windows the title-bar / taskbar icon has to be set explicitly;
    // setWindowIcon also flows into QSystemTrayIcon's tooltip-flash
    // image. On macOS we deliberately DON'T set it: Cocoa uses the
    // bundle's CFBundleIconFile (minrender.icns — squircle tile) for
    // both the inactive Dock entry and the running-app Dock entry.
    // Calling setWindowIcon here would replace the running icon with
    // the embedded transparent-bg .ico, producing an inverted look
    // mid-launch.
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/minrender.ico")));
#endif

    // Dark palette for any QtWidgets surfaces (native dialogs etc.). QML
    // rendering picks up colours from Theme.qml instead, but this keeps
    // QMessageBox / menubar chrome / etc. from flashing white.
    {
        QPalette p;
        p.setColor(QPalette::Window,          QColor(0x16, 0x16, 0x16));
        p.setColor(QPalette::WindowText,      QColor(0xcc, 0xcc, 0xcc));
        p.setColor(QPalette::Base,            QColor(0x1a, 0x1a, 0x1a));
        p.setColor(QPalette::AlternateBase,   QColor(0x1e, 0x1e, 0x1e));
        p.setColor(QPalette::Text,            QColor(0xcc, 0xcc, 0xcc));
        p.setColor(QPalette::Button,          QColor(0x22, 0x22, 0x22));
        p.setColor(QPalette::ButtonText,      QColor(0xcc, 0xcc, 0xcc));
        p.setColor(QPalette::Highlight,       QColor(0x7a, 0xa2, 0xf7));
        p.setColor(QPalette::HighlightedText, QColor(0x0f, 0x0f, 0x0f));
        p.setColor(QPalette::ToolTipBase,     QColor(0x22, 0x22, 0x22));
        p.setColor(QPalette::ToolTipText,     QColor(0xcc, 0xcc, 0xcc));
        // Mid + Dark drive the Fusion ScrollBar thumb (idle vs pressed).
        // Default Fusion derives them from Button (#222) and they end up
        // almost invisible on the #161616 surfaces — bump them so the
        // thumb reads clearly against the dark background.
        p.setColor(QPalette::Mid,             QColor(0x66, 0x66, 0x66));
        p.setColor(QPalette::Dark,            QColor(0xaa, 0xaa, 0xaa));
        p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0x66, 0x66, 0x66));
        p.setColor(QPalette::Disabled, QPalette::Text,       QColor(0x66, 0x66, 0x66));
        p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x66, 0x66, 0x66));
        QApplication::setPalette(p);
    }

    // Load bundled fonts. Family names come back from Qt's own
    // enumeration — we capture them on AppBridge so Theme.qml doesn't
    // have to guess what Inter_18pt-Regular.ttf actually registers as.
    auto loadFamily = [](const QString& path) -> QString {
        const int id = QFontDatabase::addApplicationFont(path);
        if (id < 0)
        {
            qWarning() << "Failed to load font:" << path;
            return {};
        }
        const QStringList families = QFontDatabase::applicationFontFamilies(id);
        return families.isEmpty() ? QString() : families.first();
    };

    // Bundled fonts on disk:
    //   macOS bundle:    minrender.app/Contents/Resources/fonts/
    //   Win/Linux:       <bin>/resources/fonts/
    // The fallback covers running a fresh dev build before resources/
    // has been copied — useful when stepping through main_qt.cpp under
    // a debugger that hasn't triggered the POST_BUILD step.
#ifdef Q_OS_MACOS
    QString fontDir = QCoreApplication::applicationDirPath()
                    + QStringLiteral("/../Resources/fonts/");
#else
    QString fontDir = QCoreApplication::applicationDirPath()
                    + QStringLiteral("/resources/fonts/");
#endif
    if (!QDir(fontDir).exists())
        fontDir = QCoreApplication::applicationDirPath()
                + QStringLiteral("/resources/fonts/");

    const QString interName = loadFamily(fontDir + "Inter_18pt-Regular.ttf");
    loadFamily(fontDir + "Inter_18pt-Bold.ttf");
    loadFamily(fontDir + "Inter_18pt-Italic.ttf");
    const QString monoName    = loadFamily(fontDir + "JetBrainsMono-Regular.ttf");
    // Phosphor is the primary icon font (matches ufb's UI vocabulary).
    // Material Symbols stays bundled as a fallback for the few sites that
    // already reference Theme.symbolsFamily; new code should prefer the
    // QML Icon component.
    const QString phosphorName = loadFamily(fontDir + "Phosphor.ttf");
    const QString symbolsName  = loadFamily(fontDir + "MaterialSymbolsSharp-Regular.ttf");

    // Default app font → Inter (falls through to Qt's default if Inter
    // failed to load). Tweaks to sizes happen in QML via Theme.
    if (!interName.isEmpty())
        QApplication::setFont(QFont(interName, 10));

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::warnings,
        [](const QList<QQmlError>& warnings) {
            for (const auto& w : warnings)
                std::cerr << "[QML warning] " << w.toString().toStdString()
                          << std::endl;
        });

    // Expose AppBridge to QML before loading the module so Main.qml's
    // context has `appBridge` available at first bind.
    MR::AppBridge bridge(&monitor);
    if (!interName.isEmpty())    bridge.setInterFamily(interName);
    if (!monoName.isEmpty())     bridge.setMonoFamily(monoName);
    if (!symbolsName.isEmpty())  bridge.setSymbolsFamily(symbolsName);
    if (!phosphorName.isEmpty()) bridge.setPhosphorFamily(phosphorName);
    engine.rootContext()->setContextProperty(
        QStringLiteral("appBridge"), &bridge);

    // "Check for Updates…" in the About menu drives this. The underlying
    // Sparkle/WinSparkle calls are inline no-ops unless the updater is vendored
    // (see the CMake auto-update block).
    AppUpdater appUpdater;
    engine.rootContext()->setContextProperty(
        QStringLiteral("appUpdater"), &appUpdater);

    engine.loadFromModule("MinRenderUi", "Main");

    for (QObject* obj : engine.rootObjects())
    {
        if (auto* w = qobject_cast<QWindow*>(obj))
        {
            MR::enableDarkTitleBar(w);
            // sRGB pin must run after winId() resolves to a real NSView
            // (which happens once the window is created). loadFromModule
            // returns synchronously after construction, so this is safe.
            MR::pinSRgbColorSpace(w);
        }
    }

    auto showWindow = [&engine]() {
        const auto objs = engine.rootObjects();
        if (objs.isEmpty())
            return;
        if (auto* w = qobject_cast<QWindow*>(objs.first()))
        {
            w->setVisible(true);
            w->raise();
            w->requestActivate();
        }
    };

    singleInstance.setActivationCallback(showWindow);

    MR::Tray tray;
    if (!tray.init())
        qWarning("System tray unavailable; continuing without tray");

    tray.onShowWindow = showWindow;
    tray.onStopResume = [&monitor]() {
        monitor.setNodeState(
            monitor.nodeState() == MR::NodeState::Active
                ? MR::NodeState::Stopped
                : MR::NodeState::Active);
    };
    tray.onExit = [&monitor]() {
        monitor.requestExit();
    };

#ifdef Q_OS_MACOS
    // Dock-click activation. Main.qml's onClosing hides the window via
    // setVisible(false) — Cocoa won't auto-reopen a hidden NSWindow on
    // a Dock rapp event, so without this hook the Dock icon is dead
    // until the user remembers the menu-bar tray. Qt's delegate forces
    // a state change to ApplicationActive on every rapp, even when the
    // app is already active, so this fires reliably; the isVisible
    // guard avoids redundant raises during Cmd-Tab refocus when the
    // window is already showing.
    QObject::connect(&app, &QApplication::applicationStateChanged,
        [&](Qt::ApplicationState state) {
            if (state != Qt::ApplicationActive)
                return;
            const auto objs = engine.rootObjects();
            if (objs.isEmpty())
                return;
            if (auto* w = qobject_cast<QWindow*>(objs.first());
                w && !w->isVisible())
            {
                showWindow();
            }
        });
#endif

    QGuiApplication::setQuitOnLastWindowClosed(false);

    // 50 ms pump — mirrors main_headless.cpp. The only path out of
    // app.exec() is monitor.shouldExit(): tray Exit calls requestExit(),
    // the next tick sees shouldExit() and calls QCoreApplication::quit().
    QTimer pumpTimer;
    pumpTimer.setInterval(50);
    QObject::connect(&pumpTimer, &QTimer::timeout, [&]() {
        monitor.update();
        bridge.refresh();

        tray.setTooltip(monitor.trayTooltip());
        tray.setStatusText(monitor.trayStatusText());
        tray.setNodeActive(monitor.nodeState() == MR::NodeState::Active);

        if (monitor.shouldExit())
            QCoreApplication::quit();
    });
    pumpTimer.start();

    // Start the auto-updater once the event loop is running and the UI is up.
    // On macOS this honors the Info.plist SU* keys (daily background check);
    // on Windows it configures WinSparkle's feed + key. No-op when the updater
    // isn't vendored. Deferred via singleShot so the first check never blocks
    // launch. GUI target only — the headless sidecar never calls this.
    QTimer::singleShot(0, &app, [] { MR::startSparkleUpdater(); });

    const int rc = app.exec();

    // Tear down the tray first so no queued callbacks fire into a
    // partially-destructed monitor.
    tray.shutdown();
    monitor.shutdown();
    return rc;
}

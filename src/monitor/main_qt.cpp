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
#include "ui/platform/title_bar.h"
#include "ui/platform/tray.h"

#include <QtQml/qqml.h>

#include <QApplication>
#include <QDir>
#include <QFontDatabase>
#include <QIcon>
#include <QLoggingCategory>
#include <QPalette>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlError>
#include <QQuickStyle>
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
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/minrender.ico")));

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

    const QString fontDir = QCoreApplication::applicationDirPath()
                          + QStringLiteral("/resources/fonts/");

    const QString interName = loadFamily(fontDir + "Inter_18pt-Regular.ttf");
    loadFamily(fontDir + "Inter_18pt-Bold.ttf");
    loadFamily(fontDir + "Inter_18pt-Italic.ttf");
    const QString monoName    = loadFamily(fontDir + "JetBrainsMono-Regular.ttf");
    const QString symbolsName = loadFamily(fontDir + "MaterialSymbolsSharp-Regular.ttf");

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
    engine.rootContext()->setContextProperty(
        QStringLiteral("appBridge"), &bridge);

    engine.loadFromModule("MinRenderUi", "Main");

    for (QObject* obj : engine.rootObjects())
    {
        if (auto* w = qobject_cast<QWindow*>(obj))
            MR::enableDarkTitleBar(w);
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

    const int rc = app.exec();

    // Tear down the tray first so no queued callbacks fire into a
    // partially-destructed monitor.
    tray.shutdown();
    monitor.shutdown();
    return rc;
}

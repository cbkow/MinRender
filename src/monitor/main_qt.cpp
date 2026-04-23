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
#include "ui/platform/title_bar.h"
#include "ui/platform/tray.h"

#include <QApplication>
#include <QIcon>
#include <QLoggingCategory>
#include <QQmlApplicationEngine>
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

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);

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

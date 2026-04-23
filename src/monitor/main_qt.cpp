// Qt 6 Quick GUI entry point.
//
// Linked only into the `minrender` target. The `minrender-headless` target
// uses main_headless.cpp instead.
//
// Phase 1 scope: empty ApplicationWindow with menu bar + placeholder
// panels, system tray, single-instance guard, hide-to-tray lifecycle.
// MonitorApp and AppBridge arrive in Phase 2. See docs/qt-port-plan.md.

#include "core/single_instance.h"
#include "ui/platform/title_bar.h"
#include "ui/platform/tray.h"

#include <QApplication>
#include <QIcon>
#include <QLoggingCategory>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QWindow>

#ifdef _MSC_VER
#include <crtdbg.h>
#include <cstdlib>
#include <iostream>

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

    // Use QApplication (not QGuiApplication) so QSystemTrayIcon is available
    // once we wire the tray in Phase 1.
    QApplication app(argc, argv);
    QApplication::setOrganizationName("MinRender");
    QApplication::setApplicationName("MinRender");
    QApplication::setApplicationVersion(APP_VERSION);

    // Single-instance guard. If another minrender.exe is already running,
    // signalExisting() pokes it via QLocalSocket and this process exits —
    // the first instance's activation callback (set below) pops its window.
    MR::SingleInstance singleInstance("MinRenderMonitor");
    if (!singleInstance.isFirst())
    {
        singleInstance.signalExisting();
        return 0;
    }

    // Pick the Fusion style — most customizable, dark-theme friendly,
    // cross-platform. See Phase 6 for the full theme pass.
    QQuickStyle::setStyle("Fusion");

#ifdef _WIN32
    // Icon from resources/minrender.rc — Qt picks up the Win32 icon resource
    // automatically for the taskbar and window decoration.
    app.setWindowIcon(QIcon(":/icons/minrender.ico"));
#endif

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);

    engine.loadFromModule("MinRenderUi", "Main");

    // Apply the dark title bar to every top-level QWindow the engine
    // produced. No-op off Windows.
    for (QObject* obj : engine.rootObjects())
    {
        if (auto* w = qobject_cast<QWindow*>(obj))
            MR::enableDarkTitleBar(w);
    }

    // Restore the main window from hidden state — used by both the tray's
    // Show Window menu item and the single-instance activation callback.
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

    // A second launch asks this instance to pop forward. Fires on the Qt
    // event loop, same thread as `showWindow`'s callers — safe to invoke
    // directly without invokeMethod.
    singleInstance.setActivationCallback(showWindow);

    // System tray. onStopResume is still a stub until Phase 2 (AppBridge
    // wires it to MonitorApp::setNodeState).
    MR::Tray tray;
    if (!tray.init())
    {
        qWarning("System tray unavailable; continuing without tray");
    }
    tray.onShowWindow = showWindow;
    tray.onStopResume = []() {
        qInfo("[Tray] Stop/Resume requested (backend toggle lands in Phase 2)");
    };
    tray.onExit = []() {
        QCoreApplication::quit();
    };

    // Window close hides to tray (see Main.qml onClosing). Without this,
    // closing the window would quit the app the moment the engine tears
    // down its last window.
    QGuiApplication::setQuitOnLastWindowClosed(false);

    return app.exec();
}

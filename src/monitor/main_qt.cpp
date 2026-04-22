// Qt 6 Quick GUI entry point.
//
// Linked only into the `minrender` target. The `minrender-headless` target
// uses main_headless.cpp instead.
//
// Phase 0 scope: open an empty ApplicationWindow from QML. MonitorApp,
// system tray, single-instance, and panels are added in Phase 1+.
// See docs/qt-port-plan.md.

#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QIcon>
#include <QLoggingCategory>

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

    return app.exec();
}

// Headless entry point — tray + backend only, no GUI toolkit linked.
//
// Linked only into the `minrender-headless` target. The `minrender` Qt
// target uses main_qt.cpp instead.

#include "monitor/monitor_app.h"
#include "core/system_tray.h"
#include "core/single_instance.h"

#include <QCoreApplication>

#include <iostream>
#include <thread>
#include <chrono>

#ifdef _MSC_VER
#include <cstdlib>
#include <crtdbg.h>

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

    // Minimal Qt init so QLockFile / QLocalServer (used inside SingleInstance)
    // have a thread-affinity root. No event loop runs here — headless pumps
    // its own loop below. Kept dormant; costs one Qt6Core.dll at runtime.
    QCoreApplication qtApp(argc, argv);
    QCoreApplication::setOrganizationName("MinRender");
    QCoreApplication::setApplicationName("MinRender");

    // --- Single Instance Check ---
    MR::SingleInstance singleInstance("MinRenderMonitor");
    if (!singleInstance.isFirst())
    {
        singleInstance.signalExisting();
        return 0;
    }

    // --- App ---
    MR::MonitorApp app;
    if (!app.init())
    {
        std::cerr << "Failed to initialize MonitorApp" << std::endl;
        return 1;
    }

    // --- System tray ---
    MR::SystemTray tray;
    tray.init();

    tray.onShowWindow = [&]() {
        // Headless build has no window to show.
        std::cerr << "[Headless] Show window requested (no GUI available)" << std::endl;
    };

    tray.onStopResume = [&]() {
        if (app.nodeState() == MR::NodeState::Active)
            app.setNodeState(MR::NodeState::Stopped);
        else
            app.setNodeState(MR::NodeState::Active);
    };

    tray.onExit = [&]() {
        app.requestExit();
    };

    // --- Headless main loop ---
    while (!app.shouldExit())
    {
        app.update();

        tray.setTooltip(app.trayTooltip());
        tray.setStatusText(app.trayStatusText());
        tray.setNodeActive(app.nodeState() == MR::NodeState::Active);

        // Pump Win32 messages for the tray's message-only window
#ifdef _WIN32
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
#endif

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    // --- Cleanup ---
    tray.shutdown();
    app.shutdown();

    return 0;
}

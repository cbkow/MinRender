cmake_minimum_required(VERSION 3.24)
project(MinRender VERSION 0.4.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- FetchContent dependencies (backend-only, no UI toolkit) ---
include(FetchContent)

# nlohmann/json
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(nlohmann_json)

# cpp-httplib (header-only HTTP)
FetchContent_Declare(
    httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG        v0.18.3
    GIT_SHALLOW    TRUE
)
set(HTTPLIB_COMPILE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_OPENSSL_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_BROTLI_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(httplib)

# SQLiteCpp
FetchContent_Declare(
    SQLiteCpp
    GIT_REPOSITORY https://github.com/SRombauts/SQLiteCpp.git
    GIT_TAG        3.3.2
    GIT_SHALLOW    TRUE
)
set(SQLITECPP_RUN_CPPLINT OFF CACHE BOOL "" FORCE)
set(SQLITECPP_RUN_CPPCHECK OFF CACHE BOOL "" FORCE)
set(SQLITECPP_RUN_DOXYGEN OFF CACHE BOOL "" FORCE)
set(SQLITECPP_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SQLITECPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(SQLiteCpp)

# --- Qt 6.8 (GUI target only; headless target has no Qt dependency) ---
find_package(Qt6 6.8 REQUIRED COMPONENTS
    Core
    Gui
    Network        # needed for QLocalServer / QLocalSocket in single_instance
    Qml
    Quick
    QuickControls2
    Widgets        # needed for QApplication + QSystemTrayIcon (Phase 1)
)
qt_standard_project_setup(REQUIRES 6.8)

# --- Core sources (shared between GUI and headless) ---
# NOTE: main_qt.cpp and main_headless.cpp are NOT in this list —
# each is added to exactly one target below. Same for tray: the GUI
# uses src/ui/platform/tray.cpp (Qt), headless keeps the Win32 tray
# in src/core/system_tray.cpp until Phase 8 gives headless Qt too.
set(MINRENDER_CORE_SOURCES
    src/core/platform.cpp
    src/core/node_identity.cpp
    src/core/ipc_server.cpp
    src/core/ui_ipc_server.cpp
    src/core/monitor_log.cpp
    src/core/single_instance.cpp
    src/core/net_utils.cpp
    src/core/http_server.cpp
    src/core/udp_notify.cpp
    src/monitor/peer_manager.cpp
    src/monitor/monitor_app.cpp
    src/monitor/agent_supervisor.cpp
    src/monitor/rndr_supervisor.cpp
    src/monitor/template_manager.cpp
    src/monitor/render_coordinator.cpp
    src/monitor/database_manager.cpp
    src/monitor/dispatch_manager.cpp
    src/monitor/farm_init.cpp
    src/monitor/submission_watcher.cpp
)

# --- Platform-specific link libraries ---
if(WIN32)
    set(PLATFORM_LIBS
        shell32 ole32 oleaut32 Rpcrt4 dwmapi Advapi32 Ws2_32 Iphlpapi
        dxgi bcrypt
    )
elseif(APPLE)
    find_library(COREFOUNDATION_LIB CoreFoundation)
    find_library(IOKIT_LIB IOKit)
    set(PLATFORM_LIBS
        ${COREFOUNDATION_LIB}
        ${IOKIT_LIB}
        pthread
    )
else()
    set(PLATFORM_LIBS pthread)
endif()

# ===========================================================================
# MinRender Monitor (Qt 6 Quick GUI)
# ===========================================================================
qt_add_executable(minrender
    ${MINRENDER_CORE_SOURCES}
    src/monitor/main_qt.cpp
    src/ui/app_bridge.cpp
    src/ui/models/chunks_model.cpp
    src/ui/models/jobs_model.cpp
    src/ui/models/log_model.cpp
    src/ui/models/nodes_model.cpp
    src/ui/models/templates_model.cpp
    src/ui/platform/accent_color.cpp
    src/ui/platform/title_bar.cpp
    src/ui/platform/tray.cpp
    $<$<BOOL:${WIN32}>:resources/minrender.rc>
)

# QML-only UI — no .ui forms ever, so turn AUTOUIC off so it doesn't
# misread core/ui_ipc_server.h as a UIC-generated header.
set_target_properties(minrender PROPERTIES AUTOUIC OFF)

target_include_directories(minrender PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(minrender PRIVATE
    APP_VERSION="${PROJECT_VERSION}"
    CPPHTTPLIB_THREAD_POOL_COUNT=4
)

# QML module — compiled ahead-of-time into the binary via qmlsc.
qt_add_qml_module(minrender
    URI MinRenderUi
    VERSION 1.0
    QML_FILES
        src/ui/qml/Main.qml
        src/ui/qml/panels/FarmCleanupDialog.qml
        src/ui/qml/panels/JobDetailPanel.qml
        src/ui/qml/panels/JobListPanel.qml
        src/ui/qml/panels/LogPanel.qml
        src/ui/qml/panels/NodePanel.qml
        src/ui/qml/panels/SettingsPanel.qml
        src/ui/qml/panels/SubmissionForm.qml
    RESOURCE_PREFIX /qt/qml
)

# Embed icons into the binary at :/icons/… — the Inno Setup payload still
# ships resources/ on disk for fonts, but the app window + tray read icons
# from the embedded copy so a missing resources/ dir can't break either.
qt_add_resources(minrender "app_icons"
    PREFIX "/icons"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons"
    FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/minrender.ico"
)

target_link_libraries(minrender PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Widgets
    nlohmann_json::nlohmann_json
    httplib::httplib
    SQLiteCpp
    ${PLATFORM_LIBS}
)

# Copy resources/ next to the executable at build time
add_custom_command(TARGET minrender POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/resources"
        "$<TARGET_FILE_DIR:minrender>/resources"
    COMMENT "Copying resources to build output"
)

if(WIN32)
    set_target_properties(minrender PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
if(MSVC)
    target_compile_options(minrender PRIVATE /W4)
endif()

# ===========================================================================
# MinRender Headless (sidecar — cross-platform, no GUI dependencies)
# ===========================================================================
add_executable(minrender-headless
    ${MINRENDER_CORE_SOURCES}
    src/core/system_tray.cpp
    src/monitor/main_headless.cpp
    $<$<BOOL:${WIN32}>:resources/minrender.rc>
)

# Headless has no Qt dependency; disable Qt autogen (AUTOUIC otherwise
# misinterprets core/ui_ipc_server.h as a UIC-generated header).
set_target_properties(minrender-headless PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)

target_include_directories(minrender-headless PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(minrender-headless PRIVATE
    APP_VERSION="${PROJECT_VERSION}"
    CPPHTTPLIB_THREAD_POOL_COUNT=4
    CPPHTTPLIB_NO_OPENSSL
    CPPHTTPLIB_NO_BROTLI
    MINRENDER_HEADLESS
)

target_link_libraries(minrender-headless PRIVATE
    Qt6::Core          # for QLockFile in single_instance
    Qt6::Network       # for QLocalServer / QLocalSocket in single_instance
    nlohmann_json::nlohmann_json
    httplib::httplib
    SQLiteCpp
    ${PLATFORM_LIBS}
)

add_custom_command(TARGET minrender-headless POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/resources"
        "$<TARGET_FILE_DIR:minrender-headless>/resources"
    COMMENT "Copying resources to headless build output"
)

if(MSVC)
    target_compile_options(minrender-headless PRIVATE /W4)
endif()

# ===========================================================================
# Restart Helper (Windows only, unchanged)
# ===========================================================================
if(WIN32)
    add_executable(mr-restart src/restart/main.cpp)
    if(MSVC)
        target_compile_options(mr-restart PRIVATE /W4)
        set_target_properties(mr-restart PROPERTIES WIN32_EXECUTABLE TRUE)
        set_target_properties(mr-restart PROPERTIES LINK_FLAGS "/ENTRY:mainCRTStartup")
    endif()
endif()

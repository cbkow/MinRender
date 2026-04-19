cmake_minimum_required(VERSION 3.20)
project(MinRender VERSION 0.3.10 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- FetchContent dependencies ---
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

# --- GUI-only dependencies (Windows only for now) ---
if(WIN32)
    # GLFW 3.4
    FetchContent_Declare(
        glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG        3.4
        GIT_SHALLOW    TRUE
    )
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(glfw)

    # ImGui (docking branch)
    FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG        docking
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(imgui)

    # Native File Dialog Extended
    FetchContent_Declare(
        nfd
        GIT_REPOSITORY https://github.com/btzy/nativefiledialog-extended.git
        GIT_TAG        v1.2.1
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(nfd)

    # GLAD (pre-generated, GL 3.3 Core)
    add_library(glad STATIC external/glad/src/gl.c)
    target_include_directories(glad PUBLIC external/glad/include)

    # ImGui (manual target — no upstream CMakeLists.txt)
    add_library(imgui_lib STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_demo.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    )
    target_include_directories(imgui_lib PUBLIC
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
    )
    target_link_libraries(imgui_lib PUBLIC glfw glad)
endif()

# --- Core sources (shared between GUI and headless) ---
set(MINRENDER_CORE_SOURCES
    src/core/platform.cpp
    src/core/node_identity.cpp
    src/core/ipc_server.cpp
    src/core/ui_ipc_server.cpp
    src/core/monitor_log.cpp
    src/core/system_tray.cpp
    src/core/single_instance.cpp
    src/core/net_utils.cpp
    src/core/http_server.cpp
    src/core/udp_notify.cpp
    src/monitor/peer_manager.cpp
    src/monitor/main.cpp
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
# MinRender Monitor (GUI — Windows only, uses ImGui/GLFW/OpenGL)
# ===========================================================================
if(WIN32)
    add_executable(minrender
        ${MINRENDER_CORE_SOURCES}
        src/monitor/ui/dashboard.cpp
        src/monitor/ui/settings_panel.cpp
        src/monitor/ui/node_panel.cpp
        src/monitor/ui/job_list_panel.cpp
        src/monitor/ui/job_detail_panel.cpp
        src/monitor/ui/log_panel.cpp
        src/monitor/ui/farm_cleanup_dialog.cpp
        src/monitor/ui/style.cpp
        resources/minrender.rc
    )

    target_include_directories(minrender PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    target_compile_definitions(minrender PRIVATE
        APP_VERSION="${PROJECT_VERSION}"
        CPPHTTPLIB_THREAD_POOL_COUNT=4
    )

    target_link_libraries(minrender PRIVATE
        imgui_lib
        glad
        nlohmann_json::nlohmann_json
        httplib::httplib
        SQLiteCpp
        nfd
        opengl32
        ${PLATFORM_LIBS}
    )

    # Copy resources/ next to the executable at build time
    add_custom_command(TARGET minrender POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/resources"
            "$<TARGET_FILE_DIR:minrender>/resources"
        COMMENT "Copying resources to build output"
    )

    # MSVC-specific settings
    if(MSVC)
        target_compile_options(minrender PRIVATE /W4)
        set_target_properties(minrender PROPERTIES WIN32_EXECUTABLE TRUE)
        set_target_properties(minrender PROPERTIES LINK_FLAGS "/ENTRY:mainCRTStartup")
    endif()
endif()

# ===========================================================================
# MinRender Headless (sidecar — cross-platform, no GUI dependencies)
# ===========================================================================
add_executable(minrender-headless
    ${MINRENDER_CORE_SOURCES}
    $<$<BOOL:${WIN32}>:resources/minrender.rc>
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
    nlohmann_json::nlohmann_json
    httplib::httplib
    SQLiteCpp
    ${PLATFORM_LIBS}
)

# Copy resources/ next to the executable at build time
add_custom_command(TARGET minrender-headless POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/resources"
        "$<TARGET_FILE_DIR:minrender-headless>/resources"
    COMMENT "Copying resources to headless build output"
)

if(MSVC)
    target_compile_options(minrender-headless PRIVATE /W4)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(minrender-headless PRIVATE -Wall -Wextra)
endif()

# ===========================================================================
# Restart Helper (Windows only)
# ===========================================================================
if(WIN32)
    add_executable(mr-restart src/restart/main.cpp)
    if(MSVC)
        target_compile_options(mr-restart PRIVATE /W4)
        set_target_properties(mr-restart PROPERTIES WIN32_EXECUTABLE TRUE)
        set_target_properties(mr-restart PROPERTIES LINK_FLAGS "/ENTRY:mainCRTStartup")
    endif()
endif()

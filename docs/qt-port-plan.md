# MinRender Qt 6 Quick Port Plan

Status: **Phase 1 shell complete (MinGW/Qt 6.11) — awaiting runtime smoke test before Phase 2** · Owner: Chris · Last updated: 2026-04-23

## Current state (read this first if you are a fresh agent)

Phase 0 edits are in place and both `minrender` (Qt GUI) and `minrender-headless` compile and link cleanly on Chris's machine. The commit is **not** yet made — awaiting Chris's review of the toolchain change (see below) before the single Phase 0 commit goes in.

### Toolchain change vs original plan

The plan originally locked **MSVC + Qt 6.8 msvc2022_64**. Chris's machine had only **Qt 6.11.0 mingw_64** installed, so Phase 0 was brought up on that instead:

- Compiler: MinGW-w64 GCC 13.1.0 (bundled at `C:/Qt/Tools/mingw1310_64`)
- Qt: 6.11.0 (mingw_64) at `C:/Qt/6.11.0/mingw_64`
- Generator: Ninja (bundled at `C:/Qt/Tools/Ninja`)
- CMake: 3.30.5 (bundled at `C:/Qt/Tools/CMake_64`)

Configure command used:
```
cmake -B build -S . -G Ninja \
  -DCMAKE_PREFIX_PATH="C:/Qt/6.11.0/mingw_64" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER="C:/Qt/Tools/mingw1310_64/bin/gcc.exe" \
  -DCMAKE_CXX_COMPILER="C:/Qt/Tools/mingw1310_64/bin/g++.exe"
```
PATH augmented with `C:/Qt/Tools/mingw1310_64/bin:C:/Qt/Tools/Ninja` for the build invocation.

The "Decisions (locked)" section still lists MSVC + 6.8 as historical context. The live toolchain is MinGW + 6.11. If Chris later installs Qt 6.x msvc2022_64 and wants to switch back, the changes that would need to revert are: the MinGW `_com_util::ConvertStringToBSTR` shim in `src/core/platform.cpp`, and possibly `WIN32_EXECUTABLE TRUE` moved out of the `if(MSVC)` block (harmless either way on MSVC).

### MinGW-specific fixups made during Phase 0 build-up

These were required to get the existing MSVC-era C++ building under MinGW GCC 13:

- `src/monitor/main_qt.cpp` and `src/monitor/main_headless.cpp` — CRT hooks (`_set_invalid_parameter_handler`, `_CrtSetReportMode`, `<crtdbg.h>`) guarded with `#ifdef _MSC_VER` instead of `_WIN32`, since MinGW's CRT doesn't expose them.
- `src/core/platform.cpp` — added a local `_com_util::ConvertStringToBSTR` implementation under `#if defined(_WIN32) && !defined(_MSC_VER)`. MSVC's `comsuppw.lib` provides this; MinGW doesn't ship an equivalent. `_bstr_t(const char*)` in `comdef.h` calls it.
- `CMakeLists.txt`:
  - `PLATFORM_LIBS` now includes `dxgi bcrypt` (MinGW ignores the `#pragma comment(lib, …)` directives in `node_identity.cpp` / `farm_init.cpp`).
  - `WIN32_EXECUTABLE TRUE` moved out of the `if(MSVC)` block so MinGW-built `minrender.exe` is also a windowed-subsystem app.
  - `minrender-headless` gets `AUTOMOC/AUTOUIC/AUTORCC OFF` explicitly — it has zero Qt dependency, and AUTOUIC was otherwise misreading `core/ui_ipc_server.h` as a UIC-generated header (filename starts with `ui_`).
  - `minrender` gets `AUTOUIC OFF` for the same reason — the UI is QML-only, no `.ui` forms will ever exist.

### What's in place

- `CMakeLists.txt` — rewritten for Qt 6 (pinned `find_package(Qt6 6.8 REQUIRED …)` as minimum version; 6.11 satisfies it). Only `nlohmann_json`, `cpp-httplib`, and `SQLiteCpp` FetchContent remain. CMake minimum bumped to 3.24 (needed by `qt_standard_project_setup`). Project version bumped to 0.4.0.
- `src/monitor/main_qt.cpp` — new. Minimal `QApplication` + `QQmlApplicationEngine` loading `MinRenderUi/Main`. Fusion style. No `MonitorApp` wiring yet.
- `src/monitor/main_headless.cpp` — new. Lifted from the old `main.cpp` headless branch, with the `setHeadless(true)` call removed.
- `src/ui/qml/Main.qml` — new. Empty 1440×900 `ApplicationWindow` with a placeholder label.
- `src/monitor/monitor_app.{h,cpp}` — stripped of ImGui coupling: removed `#include <imgui.h>`, `#include "monitor/ui/dashboard.h"`, `Dashboard m_dashboard`, `renderUI()`, `setHeadless`/`isHeadless`/`m_headless`, and the font-scale call. Font scale stays in `m_config`; the Qt layer will apply it via `AppBridge` in Phase 2.

### Orphans (need `git rm` before the first commit)

The sandbox couldn't delete these; CMake no longer references any of them, but they're still tracked:

```
git rm -rf src/monitor/ui/ external/glad/
git rm src/monitor/main.cpp
```

(`src/monitor/main.cpp` was overwritten with a deprecation stub pointing at `main_qt.cpp` and `main_headless.cpp`.)

### What still needs to happen for Phase 0 to be "done"

1. ~~Run the `git rm` above.~~ **Done — deletions staged.**
2. ~~Verify the GUI target builds.~~ **Done — `build/minrender.exe` links.** Runtime smoke test (window opens, placeholder label, clean close) still pending on Chris's side — the build environment verified the linker but didn't launch the binary with Qt runtime DLLs on PATH.
3. ~~Verify the headless target still builds.~~ **Done — `build/minrender-headless.exe` links.** Runtime smoke test (tray shows, farm runs) still pending on Chris's side.
4. Commit as one atomic change ("Phase 0: scaffold Qt 6.11 MinGW, remove ImGui"). Awaiting review.

### Likely failure modes at first build

- `find_package(Qt6 6.8 ...)` fails → `CMAKE_PREFIX_PATH` points at wrong directory. Adjust to actual Qt install, e.g. `C:/Qt/6.8.1/msvc2022_64`.
- `qt_standard_project_setup` unrecognized → CMake < 3.24 or Qt 6 Core module missing from install.
- `Qt6WidgetsConfig.cmake` not found → Qt install missing Widgets module. Re-run Qt maintenance tool, check "Qt Widgets" and "Qt Quick Controls 2".
- MSVC `/W4` complaining about pre-existing backend code → unrelated to Phase 0. Quickest unblock: change `/W4` to `/W3` on the `minrender` target. Don't chase this during Phase 0; note it as a pre-existing debt.
- `qt_add_qml_module` error about missing QML file → confirm `src/ui/qml/Main.qml` exists.

### If you are an agent picking this up

Start by reading the "Phase breakdown" below and the file-by-file mapping. `MonitorApp` is your stable seam (~42 public methods) — never refactor it while porting UI. Models mutate only on the UI thread via `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` — this invariant is load-bearing.

## Decisions (locked)

- **UI framework:** Qt 6 Quick/QML (not Widgets).
- **Qt version:** pin to **6.8** (latest as of this writing; good QML compiler, current platform support).
- **License:** LGPLv3 — dynamic link, ship Qt DLLs alongside, document user-replaceability. MinRender stays MIT.
- **Process model:** in-process. UI and backend in the same executable. The existing `UiIpcServer` / "Tauri Phase 2" scaffold will be removed during decommission — not a target to aim for.
- **No dockable panels.** Current ImGui docking is a holdover; `SplitView` with show/hide toggles is the replacement. Tearoff, floating, rearrange: not supported, not missed.
- **Cut ImGui loose from Phase 0.** No parallel builds, no `MINRENDER_USE_QT` option. ImGui code (`src/monitor/ui/`, `external/glad/`, FetchContent for GLFW/ImGui/NFD) is deleted in Phase 0. The Qt target starts empty and grows panel-by-panel. `minrender-headless` keeps running the full farm through the whole port — the headless target is the fallback while the UI is under construction.
- **`mr-restart` unchanged.** It's a Windows-only headless sidecar that relaunches the app. Not blocking cross-platform, not worth refactoring.
- **Cross-platform trajectory:** Windows first (parity with today), macOS and Linux as Phase 8 after the port lands.

## Goals

1. Eliminate ImGui's continuous-redraw GPU tax on render nodes.
2. Keep `MonitorApp` and all backend code unchanged. This is a UI-layer swap.
3. Leave the door open for macOS and Linux without refactoring anything done here.
4. End with a cleaner build: fewer FetchContent deps, cross-platform file dialog, cross-platform single-instance, cross-platform tray.

## Non-goals (for this port)

- Rearrangeable/tearoff/floating docking.
- Animation polish, fluid transitions, touch support.
- Changes to the HTTP API, UDP protocol, Rust agent protocol, or DB schema.
- New features. Port scope only.
- Out-of-process UI over `UiIpcServer`.
- macOS/Linux shipping artifacts. (Phase 8.)

## Architecture

### Process model

Single process. `QApplication` (not `QGuiApplication` — we want `QSystemTrayIcon` which lives in QtWidgets) + `QQmlApplicationEngine`. `MonitorApp::update()` runs on a `QTimer` with 50 ms interval, matching today's headless loop. Existing background threads (HTTP server, HTTP worker, agent IPC reader) are untouched.

### Layers

- **Backend (C++, untouched):** `MonitorApp` and all its managers. No Qt macros, no `Q_OBJECT`, still compiles into `minrender-headless` cleanly.
- **Bridge (C++, new):** `AppBridge : QObject` wrapping `MonitorApp*`. All `Q_PROPERTY` / `Q_INVOKABLE` / `signals` live here so the backend stays Qt-free.
- **Models (C++, new):** `QAbstractListModel` subclasses wrapping the cached vectors in `MonitorApp`. Surgical `dataChanged`/`rowsInserted`/`rowsRemoved` emission, not full resets.
- **View (QML, new):** panels, components, theme singleton. One `.qml` per panel, shared components in `components/`.
- **Platform shim (C++, new):** `ui/platform/` holds every `#ifdef _WIN32/__APPLE__/__linux__` block so OS conditionals don't leak into panel code.

### Directory structure

```
src/
  core/                         (unchanged)
  monitor/                      (backend — unchanged)
    main.cpp                    (rewrite: QApplication entrypoint)
    monitor_app.*               (unchanged)
    ...
  ui/                           (NEW)
    app_bridge.{h,cpp}          QObject wrapping MonitorApp
    models/
      jobs_model.{h,cpp}
      nodes_model.{h,cpp}
      chunks_model.{h,cpp}
      log_model.{h,cpp}
      templates_model.{h,cpp}
    painters/
      frame_grid.{h,cpp}        QQuickPaintedItem subclass
    platform/
      accent_color.{h,cpp,mm}   per-OS accent color query
      title_bar.{h,cpp}         per-OS dark title bar (no-op outside Windows)
      tray.{h,cpp}              thin QSystemTrayIcon wrapper
    qml/                        (Qt resource, compiled)
      Main.qml
      Theme.qml                 singleton
      panels/
        NodePanel.qml
        JobListPanel.qml
        JobDetailPanel.qml
        LogPanel.qml
        SettingsPanel.qml
        FarmCleanupDialog.qml
      components/
        OutlineButton.qml
        PanelHeader.qml
        StatusBadge.qml
        IconText.qml
        FrameGrid.qml           wraps the QQuickPaintedItem
resources/
  fonts/                        (existing — Inter, JetBrains Mono, Material Symbols)
  icons/                        (existing — add .icns, .png for mac/linux)
  qml.qrc                       (new — QML + component resources)
```

### Build targets

- `minrender-headless` — unchanged, still the pure cross-platform core.
- `minrender` — replaces the ImGui target. Qt-based. On Windows produces `.exe`, on macOS will produce `.app` bundle, on Linux will produce ELF + `.desktop`.
- `mr-restart` — Windows sidecar, unchanged.
- **Removed:** `imgui_lib`, `glad` (from `external/`), `nfd`, `glfw` FetchContent blocks.

### CMake changes (summary)

- Add `find_package(Qt6 6.7 COMPONENTS Core Gui Qml Quick QuickControls2 Widgets REQUIRED)`.
- Add CMake option `MINRENDER_USE_QT` (default OFF during Phase 0–6, flipped ON in Phase 7).
- Use `qt6_add_executable`, `qt6_add_qml_module` — enables QML-to-C++ compilation (qmlsc), measurable startup win.
- `CMAKE_AUTOMOC ON` for the `ui/` tree.

## Cross-platform considerations (woven in from Phase 0)

### Tray

- `QSystemTrayIcon` on all three platforms.
- **Known Linux limitation:** GNOME (most common desktop) removed native tray support; users need AppIndicator or TopIcons extension. Qt 6 uses StatusNotifierItem over D-Bus, falls back gracefully. On KDE/XFCE it works out of the box.
- Plan: call `QSystemTrayIcon::isSystemTrayAvailable()`. If false, don't hide on close — keep the window visible. Document the GNOME caveat in README when macOS/Linux ship.

### Dark title bar

- Windows: `DwmSetWindowAttribute(DWMWA_USE_IMMERSIVE_DARK_MODE)` — keep the existing code, wrap cleanly in `#ifdef _WIN32` inside `ui/platform/title_bar.cpp`.
- macOS: Qt 6 handles the appearance API automatically. No-op.
- Linux: theme-driven, no-op.

### Accent color

- Windows: `DwmGetColorizationColor` (existing).
- macOS: `NSColor.controlAccentColor` via a small `.mm` file (Objective-C++). Dynamic color, updates with system preference.
- Linux: `QGuiApplication::palette().color(QPalette::Highlight)` as fallback. Imperfect but honors desktop theme.
- All three live in `ui/platform/accent_color.*`; `Theme.qml` reads the result via `AppBridge`.

### Single-instance

- Today: Win32 `FindWindowEx("MinRenderTray")`.
- Replace with `QLockFile` + `QLocalServer` / `QLocalSocket` — cross-platform, same semantics. Rewrite `src/core/single_instance.*` during Phase 1.

### File dialogs

- `QFileDialog` replaces NFD everywhere. Drops the NFD FetchContent.

### Icons

- Material Symbols Sharp TTF works on all three platforms via `QFontDatabase::addApplicationFont`.
- App icon: Windows `.ico` exists. macOS `.icns` and Linux `.png` added during Phase 1 (placeholder, can refine later).

### Paths

- Replace `GetModuleFileNameW` in `style.cpp` with `QCoreApplication::applicationDirPath()`.
- QML resources go through Qt's resource system (`qml.qrc`) — compiled into the binary. Fonts stay on disk to match existing deployment.

### Rust agent IPC (NOT UI port scope)

- `mr-agent/src/ipc/windows.rs` uses named pipes. A future `unix.rs` module will use Unix domain sockets. Same for C++ side (`src/core/ipc_server.cpp`).
- Call this out in Phase 8, track as its own workstream.

### Installer

- Windows: existing Inno Setup script gets updated with `windeployqt`'s Qt DLL manifest.
- macOS: `macdeployqt` + `.pkg` + notarization. Phase 8.
- Linux: AppImage first (simplest, no root install), `.deb`/`.rpm` if demand appears. Phase 8.

## Phase breakdown

All day estimates assume focused solo work. Add ~30% for context-switching.

### Phase 0 — CMake scaffold & minimal shell · 1 day · **edits applied, build unverified**

Status summary is in the "Current state" section at the top. The code changes below have all been applied:

- [x] Delete `src/monitor/ui/` entirely. *(files orphaned in tree — run `git rm` as described above)*
- [x] Delete `external/glad/`. *(orphaned, same)*
- [x] Remove ImGui/GLFW/NFD FetchContent blocks from `CMakeLists.txt`.
- [x] Rewrite `CMakeLists.txt`: `find_package(Qt6 6.8 ...)`, `qt_standard_project_setup()`, `qt_add_executable(minrender)`, `qt_add_qml_module(...)`.
- [x] Strip ImGui references from `MonitorApp`.
- [x] Split `main.cpp` into `main_qt.cpp` and `main_headless.cpp`. No `#ifdef MINRENDER_HEADLESS` switch in either file.
- [x] `main_qt.cpp` opens an empty window via `QApplication` + `QQmlApplicationEngine`.
- [x] `Main.qml` is an empty `ApplicationWindow` with a placeholder label.
- [x] **`minrender-headless` build verified** — links on MinGW/Qt 6.11. Runtime smoke test pending.
- [x] **`minrender` (Qt) build verified** — links on MinGW/Qt 6.11. Runtime smoke test pending.
- [ ] **Commit applied** — pending Chris's review of the toolchain change.

### Phase 1 — Shell, tray, lifecycle · 3–5 days · **code complete, runtime smoke test pending**

Commits (chronological):
- `ef50f4e` Step 2a — `QLockFile` + `QLocalServer` single_instance. Headless gained `Qt6::Core`+`Qt6::Network` link and a bare `QCoreApplication`.
- `6c90177` Step 2b — `src/ui/platform/tray.{h,cpp}` wraps `QSystemTrayIcon`. Legacy Win32 `src/core/system_tray.cpp` is now headless-only (moved out of MINRENDER_CORE_SOURCES).
- `9f7dab5` Step 2c — `Main.qml` menu bar (File/Jobs/View/Help) + nested SplitView with 4 tinted placeholder rectangles. `QtCore.Settings` persists split sizes and per-panel visibility.
- `2eb37d8` Step 2d — `ApplicationWindow.onClosing` hides instead of quits; tray Show Window + SingleInstance activation callback share a `showWindow` lambda that restores/raises/activates the root QWindow.
- `1bbee06` Step 2e — `src/ui/platform/title_bar.{h,cpp}` calls `DwmSetWindowAttribute` (20, fallback 19) for dark non-client area on Windows; no-op on macOS/Linux.
- `f747411` Step 2f — `qt_add_resources` bundles `minrender.ico` at `:/icons/minrender.ico`. Tray and `QApplication::setWindowIcon` both resolve through the embedded resource.

Outstanding:
- Runtime smoke test (hide-to-tray, tray restore, second-launch activation, dark title bar actually renders dark). Link succeeds; behavior not yet verified on real launch.
- `onStopResume` in main_qt.cpp is still a log stub — lands in Phase 2 when AppBridge ties to `MonitorApp::setNodeState`.
- **Milestone:** launch, minimize to tray, restore from tray, exit cleanly. Panels are still empty.

### Phase 2 — AppBridge + Settings · 3–4 days

- `src/ui/app_bridge.{h,cpp}` — `QObject` holding a `MonitorApp*`.
- First batch of bindings, only what Settings needs:
  - `Q_PROPERTY(bool farmRunning READ ...  NOTIFY farmRunningChanged)`
  - `Q_PROPERTY(QString syncRoot READ ... WRITE ... NOTIFY ...)`
  - `Q_PROPERTY(QString tagsCsv ...)`, `httpPort`, `ipOverride`, `udpEnabled`, `udpPort`, `showNotifications`, `stagingEnabled`, `rndrDualMode`, `fontScale`, `accentColor`.
  - `Q_INVOKABLE void saveSettings()`, `Q_INVOKABLE void requestRestart()`.
- `SettingsPanel.qml` — `ColumnLayout` of `TextField` / `CheckBox` / `ComboBox` / `SpinBox` bound to bridge properties. Folder picker via `QtQuick.Dialogs`' `FolderDialog`.
- Two-way sync: QML edits → bridge setters → `MonitorApp::config()` → `saveSettings()` on explicit save (no auto-save).
- **Milestone:** Settings functional end-to-end, config round-trips to `%LOCALAPPDATA%/MinRender/config.json`.

### Phase 3 — Models · 4–6 days

Models live in `src/ui/models/`. All mutations go through `QMetaObject::invokeMethod(model, ..., Qt::QueuedConnection)` to the UI thread — the rule is strict.

- `JobsModel` — wraps `m_cachedJobs`. Roles: `name`, `slug`, `state`, `progress`, `totalChunks`, `doneChunks`, `createdAt`, `jobId`. Hooks into `refreshCachedJobs()`.
- `NodesModel` — wraps `PeerManager` peer list. Roles: `nodeId`, `hostname`, `isLeader`, `isActive`, `agentHealth`, `tags`, `lastSeen`, `isThisNode`. Updated from UDP heartbeat + HTTP status polls.
- `ChunksModel` — per-job, lazy-loaded when a job is selected. Roles: `chunkId`, `frameStart`, `frameEnd`, `state`, `assignedNode`, `progress`. Refresh on 3s cadence for active jobs, matching current behavior.
- `LogModel` — capped ring buffer (say 5000 entries). Roles: `timestamp`, `level`, `category`, `message`. Hooks into `MonitorLog::setCallback`.
- `TemplatesModel` — job templates, loaded once at farm start, refreshed on `SubmissionWatcher` events. Roles: `name`, `dcc`, `path`, `flagCount`.
- Expose each to QML via `qmlRegisterType` + `AppBridge::jobsModel()` etc.
- **Milestone:** bind a bare `ListView` to each model and see rows appear/update/disappear when backend state changes.

### Phase 4 — Remaining panels (no JobDetail) · 2 weeks

Port in order, each independent:

1. **Log panel** (~2 days). `ListView` bound to `LogModel`. Delegate: monospaced text with level-colored prefix. Filter chips for `info`/`warn`/`error`/`debug`. Autoscroll toggle. Copy-visible-to-clipboard.
2. **Node panel** (~2–3 days). Two sections: "This Node" (static `AppBridge` properties) and a `ListView` bound to `NodesModel` for peers. Per-peer menu for Unsuspend.
3. **Job list panel** (~3–4 days). `TableView` (Qt Quick 6.5+ standard table) bound to `JobsModel`. Columns: Name, State, Progress, Created. Multi-select with checkbox column. Context menu (pause/resume/cancel/delete/requeue). "New Job" button → `AppBridge.requestSubmissionMode()`.
4. **Farm Cleanup dialog** (~2 days). `Dialog` modal with file preview list and confirm/destructive action.
5. **Submission mode** (~3 days). Form rendered inside JobDetail panel's Empty/Submission branch (panel itself is Phase 5, but this subtree can be built first as a standalone `Loader`). Template picker, dynamic flag fields, frame range, chunk size, output pattern resolution, submit handling (sync on leader, async via `postToLeaderAsync` on workers).

### Phase 5 — JobDetail + frame grid · 1–1.5 weeks

- `JobDetailPanel.qml` — three-state (Empty / Submission / Detail). Submission mode built in Phase 4 drops in here.
- Detail mode: header (name, state, badges, action buttons), tab bar for Frames/Chunks, content area.
- **Frame grid** (`src/ui/painters/frame_grid.{h,cpp}`):
  - `class FrameGrid : public QQuickPaintedItem`
  - Properties: `ChunksModel* model`, `int frameStart`, `int frameEnd`, `int cellsPerRow` (computed from width).
  - `paint(QPainter*)`: iterate chunks, draw colored rectangles per frame state (matches current color table in README).
  - `update()` called only from `QAbstractItemModel::dataChanged` — not every frame, not per timer.
  - Performance target: zero measurable GPU/CPU at idle; <5 ms repaint for 10,000 frames. If too slow, escalate to scene-graph nodes (`QQuickItem::updatePaintNode`).
- Chunk table (`TableView` bound to `ChunksModel`) below the grid. Context menu: Reassign, Submit Chunk as Job, Retry.
- Async submission state handled via signals on `AppBridge` (`Q_INVOKABLE` kicks off, signal reports back `submissionSucceeded`/`submissionFailed`).
- **Milestone:** full JobDetail functional, frame grid benchmarked.

### Phase 6 — Theme, fonts, QML components · 3–5 days

- `Theme.qml` singleton: every color from current `style.cpp` as properties (`Theme.bg`, `Theme.border`, `Theme.accent`, etc.).
- `QQuickStyle::setStyle("Fusion")` in `main_qt.cpp`. Fusion is the most customizable built-in style; dark theme applies cleanly.
- `QPalette` set on `QApplication` for dark look across any QtWidgets surfaces (dialogs, file picker on some platforms).
- Load Inter / JetBrains Mono / Material Symbols via `QFontDatabase::addApplicationFont` at startup. Expose family names via `AppBridge` (or hardcode as constants).
- Build reusable components:
  - `OutlineButton.qml` — replaces `PushOutlineButtonStyle`.
  - `PanelHeader.qml` — replaces `panelHeader()` in `style.cpp`.
  - `StatusBadge.qml` — replaces `drawStatusBadge`.
  - `IconText.qml` — Material Symbols glyph + label.
  - `SectionHeader.qml` — replaces `CollapsingHeaderWithIcon`.
- Windows accent color pulled from `ui/platform/accent_color.*` at startup, pushed into `Theme.accent`.
- **Milestone:** side-by-side visual diff vs ImGui acceptable. Any nits get filed as follow-up, not blocking.

### Phase 7 — Decommission, installer · 1 week

(ImGui / GLFW / NFD / glad removal already happened in Phase 0. Phase 7 is about shipping.)

- Delete `src/core/ui_ipc_server.*` and the "Tauri Phase 2" scaffolding (since in-process is locked).
- Update `THIRD_PARTY_NOTICES.txt` — add Qt 6 LGPL notice, remove ImGui/GLFW/GLAD/NFD.
- Add `windeployqt` step to the pre-Inno-Setup build.
- Update `installer/minrender_installer.iss` to include Qt DLLs + platform plugin + QML module binaries.
- Update `README.md` — remove ImGui mention.
- Smoke-test install / uninstall / upgrade-from-prior-version on a clean Windows VM.
- Bump version (0.4.0 — first Qt release).

### Phase 8 — macOS + Linux · not part of this port

Tracked so it stays visible.

- `macdeployqt` + `.app` bundle + notarization + `.pkg` or `.dmg`.
- Linux AppImage (simplest) and `.desktop` integration.
- Port `mr-agent/src/ipc/` to add a `unix.rs` module using Unix domain sockets.
- Port `src/core/ipc_server.cpp` to Unix socket on non-Windows.
- Retire `minrender-tray/` Swift scaffold — replaced by `QSystemTrayIcon`.
- Linux DWM-dark-title-bar equivalent is a no-op; macOS handles automatically; nothing to do.
- GNOME tray limitation documented in README.

## File-by-file mapping

| Today (ImGui) | Tomorrow (Qt Quick) |
|---|---|
| `src/monitor/main.cpp` (GUI branch) | `src/monitor/main.cpp` (rewrite: `QApplication` + `QQmlApplicationEngine`) |
| `src/monitor/ui/dashboard.{h,cpp}` | `src/ui/qml/Main.qml` + menu-bar QML |
| `src/monitor/ui/style.{h,cpp}` | `src/ui/qml/Theme.qml` + `src/ui/platform/{title_bar,accent_color}.*` |
| `src/monitor/ui/ui_macros.h` | `src/ui/qml/components/*.qml` |
| `src/monitor/ui/settings_panel.*` | `src/ui/qml/panels/SettingsPanel.qml` + `AppBridge` settings properties |
| `src/monitor/ui/node_panel.*` | `src/ui/qml/panels/NodePanel.qml` + `src/ui/models/nodes_model.*` |
| `src/monitor/ui/job_list_panel.*` | `src/ui/qml/panels/JobListPanel.qml` + `src/ui/models/jobs_model.*` |
| `src/monitor/ui/job_detail_panel.*` | `src/ui/qml/panels/JobDetailPanel.qml` + `src/ui/models/chunks_model.*` + `src/ui/painters/frame_grid.*` |
| `src/monitor/ui/log_panel.*` | `src/ui/qml/panels/LogPanel.qml` + `src/ui/models/log_model.*` |
| `src/monitor/ui/farm_cleanup_dialog.*` | `src/ui/qml/panels/FarmCleanupDialog.qml` |
| `src/core/system_tray.*` | `src/ui/platform/tray.*` (thin `QSystemTrayIcon` wrapper) |
| `src/core/single_instance.*` | Rewrite using `QLockFile` + `QLocalSocket` |
| `src/core/ui_ipc_server.*` | **Deleted** (in-process; no IPC UI) |
| `external/glad/` | **Deleted** |
| ImGui / GLFW / NFD FetchContent | **Deleted** |

## Risks & mitigations

1. **QML learning curve for solo C++ dev.** Phases 0–2 are deliberately small. If QML feels hostile by end of Phase 2, it's cheap to stop and reconsider — none of the backend has been touched.
2. **Frame grid performance.** Benchmark early in Phase 5 at 10 k frames. If `QQuickPaintedItem` is sluggish, escalate to scene-graph `updatePaintNode` returning `QSGGeometryNode` for batched quads.
3. **GNOME tray.** Known limitation, document and degrade. Not blocking the port.
4. **LGPL accidents.** Stick to `Core`, `Gui`, `Qml`, `Quick`, `QuickControls2`, `Widgets`. Avoid any `Qt*Commercial` modules. Ship Qt as separate DLLs, not statically linked.
5. **Every Windows `#ifdef` becomes a cross-platform debt.** `ui/platform/` is the single place OS conditionals live. Review PRs for leakage.
6. **Startup time regression.** Qt startup is slower than ImGui. Mitigations: `qt_add_qml_module` with ahead-of-time compilation, lazy-load panels via `Loader { active: panel.visible }`, benchmark on cold start.
7. **Model thread-safety.** One rule: models mutate only on the UI thread via `QMetaObject::invokeMethod(model, ..., Qt::QueuedConnection)`. Code-review every model change against this invariant.

## Open questions for Chris

All four original open questions are resolved:

1. ~~Qt version~~ → **6.8**.
2. ~~Windows toolchain~~ → MSVC (unchanged from current build).
3. ~~`mr-restart.exe`~~ → unchanged, headless sidecar stays as-is.
4. ~~Keep ImGui target during port~~ → no, cut loose at Phase 0.

Remaining open:

- Pair-programming vs PR review cadence during the port — TBD by Chris.

## Rough timeline

Assuming solo work and typical week-on/week-off cadence:

- Phases 0–1 (scaffold + shell): ~1 week.
- Phase 2 (bridge + Settings): ~1 week.
- Phase 3 (models): ~1 week.
- Phase 4 (most panels): ~2 weeks.
- Phase 5 (JobDetail + grid): ~1.5 weeks.
- Phase 6 (style): ~1 week.
- Phase 7 (decommission + installer): ~1 week.

**Total: 8–9 weeks** of focused work to ship Qt-on-Windows. Cross-platform Phase 8 is a separate track, plausibly another 3–5 weeks depending on how much polish the mac/linux installers need.

---

## Handoff — where to start in Claude Code

Written for an agent picking this up fresh (Chris or a new Claude session). Assumes the Phase 0 edits in this repo are present (if `docs/qt-port-plan.md` exists and says "Phase 0 applied — awaiting first build" at the top, they are).

### Step 1: verify Phase 0 (before writing any new code)

Do the orphan cleanup and run both builds. Don't proceed to Phase 1 until `minrender.exe` opens an empty Qt window and `minrender-headless.exe` still runs the farm:

```
git rm -rf src/monitor/ui/ external/glad/
git rm src/monitor/main.cpp

cmake -B build -S . -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="C:/Qt/6.8.0/msvc2022_64"
cmake --build build --target minrender-headless --config Release
cmake --build build --target minrender --config Release
```

If the Qt build fails, check the "Likely failure modes" list in the Current state section above. If `minrender-headless` fails, that's a regression from the `MonitorApp` strip — most likely cause is a dangling include. Grep the tree for `dashboard.h`, `renderUI`, `setHeadless`, `m_headless`, `imgui.h` and remove anything that comes back outside `src/monitor/ui/` (which is orphaned anyway).

Commit when both pass. Use a single commit:

```
git add CMakeLists.txt src/monitor/main_qt.cpp src/monitor/main_headless.cpp \
        src/monitor/monitor_app.h src/monitor/monitor_app.cpp \
        src/ui/qml/Main.qml docs/qt-port-plan.md
git commit -m "Qt 6.8 Quick port — Phase 0: scaffold, remove ImGui"
```

(The `git rm`s above are already staged and will be in the same commit.)

### Step 2: begin Phase 1 — shell + tray + lifecycle

Concrete file-creation order. Each of these is small; commit after each so it's easy to bisect if something breaks.

**Step 2a — rewrite single-instance on Qt primitives.** Replace the Win32 `FindWindowEx` in `src/core/single_instance.{h,cpp}` with `QLockFile` + `QLocalServer` / `QLocalSocket`. Keep the `MR::SingleInstance` class API the same so `main_headless.cpp` keeps compiling. This is the first cross-platform win — the old code was Windows-only.

- Interface to preserve: `SingleInstance(const std::string& name)`, `bool isFirst()`, `void signalExisting()`, signal to a callback when a second instance asks the first to show.
- `QLockFile` goes in `QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation)` or `TempLocation` on Windows.
- `QLocalSocket` connection is the "please come forward" signal.

**Step 2b — Qt system tray.** Create `src/ui/platform/tray.{h,cpp}` wrapping `QSystemTrayIcon`. Keep the interface shape of the existing `MR::SystemTray` (callbacks `onShowWindow`, `onStopResume`, `onExit`; setters `setTooltip`, `setStatusText`, `setNodeActive`). `main_qt.cpp` wires it up. The old `src/core/system_tray.{h,cpp}` stays for `minrender-headless` until Phase 8 macOS/Linux — that's when headless gets the Qt tray too.

- `QApplication` in `main_qt.cpp` is required (not `QGuiApplication`) — `QSystemTrayIcon` lives in QtWidgets.
- Icon loaded from `resources/icons/minrender.ico` via `QIcon`.
- Context menu: Show Window, Stop/Resume, Exit.
- On Linux (future), call `QSystemTrayIcon::isSystemTrayAvailable()` first and degrade to no-hide-on-close if false.

**Step 2c — `Main.qml` shell.** Replace the Phase 0 placeholder with the real structure:

```
ApplicationWindow
├── menuBar: MenuBar (File / Jobs / View / Help)
├── SplitView (horizontal)
│   ├── Rectangle (id: nodePanelPlaceholder, SplitView.preferredWidth: width * 0.28)
│   └── SplitView (vertical)
│       ├── Rectangle (id: jobListPlaceholder, SplitView.preferredHeight: height * 0.33)
│       └── SplitView (horizontal)
│           ├── Rectangle (id: jobDetailPlaceholder)
│           └── Rectangle (id: logPlaceholder)
```

Wire View menu items to toggle `visible` on each placeholder. Save split sizes via `Settings { }` QML type (persists to QStandardPaths, fine for this scope).

**Step 2d — lifecycle.** `ApplicationWindow.onClosing` hides window instead of exiting. Tray "Show Window" un-hides. Actual `app.exit()` only from tray Exit menu.

**Step 2e — Windows dark title bar.** Port `enableDarkTitleBar` from the old `style.cpp` into `src/ui/platform/title_bar.{h,cpp}`. Call `DwmSetWindowAttribute` on `QWindow::winId()`. Wrap in `#ifdef _WIN32`. macOS/Linux versions are no-ops.

**Step 2f — app icon.** Set via `QApplication::setWindowIcon(QIcon(":/icons/minrender.ico"))` in `main_qt.cpp`. On Windows the icon resource in `minrender.rc` handles the .exe icon; `setWindowIcon` handles taskbar/Alt-Tab. Already stubbed in `main_qt.cpp` — confirm it resolves correctly after adding the icon to a Qt resource file.

**Milestone:** launch, empty shell with 4 placeholder panels, menu bar, tray. Close hides, tray Show Window restores, tray Exit quits. No backend wiring yet.

### Step 3: begin Phase 2 — AppBridge + Settings

Only after Phase 1 milestone passes. See "Phase 2" section above.

### Ground rules

- `MonitorApp` is the stable seam (~42 public methods). Never refactor it while porting UI.
- Models mutate only on the UI thread via `QMetaObject::invokeMethod(model, ..., Qt::QueuedConnection)`. Load-bearing invariant.
- All OS-conditional code goes in `src/ui/platform/`. If you find yourself typing `#ifdef _WIN32` elsewhere, stop and factor it.
- Don't pull in `KDDockWidgets` or anything to re-create docking. SplitView + visibility toggles is the plan.
- LGPL: only link `Core`, `Gui`, `Qml`, `Quick`, `QuickControls2`, `Widgets`. Anything else, check the license.
- Commit per step (2a, 2b, …). Small commits make bisection cheap if something breaks.

### When to update this doc

- After each phase milestone passes, update the status line at the top and the checkboxes in that phase.
- If a decision changes (e.g. Qt version bump, new open question), update "Decisions (locked)" and note the date.
- If a new class/file gets added that future phases will depend on, add it to the file-by-file mapping table.
- Keep "Current state" accurate — it's the first thing an agent or future-you reads.

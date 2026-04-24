# MinRender Qt 6 Quick Port Plan

Status: **Phase 5 JobDetail + FrameGrid landed — Phase 6 theme next** · Owner: Chris · Last updated: 2026-04-24

## Current state (read this first if you are a fresh agent)

Phase 0 edits are in place and both `minrender` (Qt GUI) and `minrender-headless` compile and link cleanly on Chris's machine. The commit is **not** yet made — awaiting Chris's review of the toolchain change (see below) before the single Phase 0 commit goes in.

### Toolchain

Per the plan's locked decision: MSVC + Qt msvc2022_64 + Ninja.

- Compiler: MSVC 14.50 (VS 2026 / VS 18 Community) at `C:\Program Files\Microsoft Visual Studio\18\Community`
- Qt: 6.11.0 (msvc2022_64) at `C:/Qt/6.11.0/msvc2022_64` — 6.11 satisfies the `find_package(Qt6 6.8 …)` minimum pinned in CMakeLists.txt
- Generator: Ninja (bundled at `C:/Qt/Tools/Ninja`)
- CMake: 3.30.5 (bundled at `C:/Qt/Tools/CMake_64`)

Configure / build go through `scripts\build_msvc.bat`, which loads `vcvarsall.bat x64` and forwards args to `cmake`:
```
scripts\build_msvc.bat -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_PREFIX_PATH=C:/Qt/6.11.0/msvc2022_64 ^
    -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl

scripts\build_msvc.bat --build build --target minrender --config Release
scripts\build_msvc.bat --build build --target minrender-headless --config Release
```

Runtime needs `C:/Qt/6.11.0/msvc2022_64/bin` on PATH.

### Toolchain history (for git archaeologists)

Phase 0 through Phase 4 built under MinGW 13.1.0 + Qt 6.11 mingw_64 because that was what happened to be installed. Phase 4 smoke-testing turned up that MinGW's libstdc++ `std::filesystem::is_directory` returns false for valid UNC paths (`\\server\share\…`), which broke the auto-start-farm flow on network-share sync roots — the app's primary configuration. Qt msvc2022_64 + VS 2026 were installed and the build retrofitted back to MSVC. MinGW-only scaffolding that was removed at the switch:

- `_com_util::ConvertStringToBSTR` shim in `src/core/platform.cpp` (MSVC ships it via `comsuppw`).
- `target_compile_options(... PRIVATE -Wall -Wextra)` fallback blocks in `CMakeLists.txt` for the `Clang|GNU` case.

Scaffolding that stayed because it's compiler-agnostic or useful regardless:
- CRT hooks (`_set_invalid_parameter_handler`, `_CrtSetReportMode`) in main_qt / main_headless guarded by `#ifdef _MSC_VER` — they only activate on MSVC anyway.
- `dxgi` / `bcrypt` in `PLATFORM_LIBS` — duplicates of `#pragma comment(lib, …)` in the sources, but harmless.
- `WIN32_EXECUTABLE TRUE` outside any `if(MSVC)` guard — needed on any Windows compiler.
- `AUTOUIC OFF` on `minrender`, `AUTOMOC/AUTOUIC/AUTORCC OFF` on `minrender-headless` — CMake/Qt autogen filename heuristic misreads `core/ui_ipc_server.h`, compiler-independent.

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

### Phase 2 — AppBridge + Settings · 3–4 days · **code complete, runtime smoke test pending**

Commits (chronological):
- `5fc9981` Step 3a — `MonitorApp` instantiated after `SingleInstance`, `init()` runs before the QML engine loads, a 50 ms `QTimer` drives `update()` and syncs tray state. Tray `onStopResume` now toggles `setNodeState` (previously a log stub); `onExit` calls `requestExit()` for a clean shutdown sequence.
- `c65b073` Step 3b — `src/ui/app_bridge.{h,cpp}` Q_PROPERTY set: `farmRunning`, `accentColor` (both read-only), plus the full settings surface (`syncRoot`, `tagsCsv`, `httpPort`, `ipOverride`, `udpEnabled`, `udpPort`, `showNotifications`, `stagingEnabled`, `rndrDualMode`, `fontScale`). `Q_INVOKABLE saveSettings` / `revertSettings` / `requestRestart`. Snapshot-based revert (copy of `Config` refreshed at construction and after each save). `src/ui/platform/accent_color.{h,cpp}` wraps `DwmGetColorizationColor` (force opaque) with a `QPalette::Highlight` fallback. Exposed to QML via `setContextProperty("appBridge")`.
- `0b5e06e` Step 3c — `src/ui/qml/panels/SettingsPanel.qml` bound to the bridge. Groups: Sync root (TextField + FolderDialog Browse), Tags, Networking (HTTP port, IP override, UDP enable + port), Rendering (staging, RNDR dual), UI (notifications, font scale with decimal-formatted SpinBox).
- `e8b4212` Step 3d — File → Settings opens a modal `Dialog` containing a `Loader`-wrapped `SettingsPanel` that re-instantiates on each open (fresh bindings). `closePolicy: NoAutoClose` forces Save/Cancel; Cancel calls `revertSettings()` before close so no in-memory edits leak past the panel.

Not yet in Phase 2 scope, deferred to the relevant later phase:
- Live-updating accent color on WM_SETTINGCHANGE (Phase 6 theme).
- `MonitorApp::reloadConfig()` — current revert copies the snapshot back in-memory, which is correct for this phase's scope (user-edit-then-cancel) but can't recover if an external process rewrites `config.json` mid-session. No known use case needs that today.
- **Milestone:** Settings functional end-to-end, config round-trips to `%LOCALAPPDATA%/MinRender/config.json` on Save, reverts cleanly on Cancel.

### Phase 3 — Models · 4–6 days · **code complete, runtime smoke test pending**

All five models live under `src/ui/models/`. AppBridge owns them as `unique_ptr`s and exposes each as a CONSTANT `Q_PROPERTY` (`appBridge.jobsModel`, `nodesModel`, `logModel`, `chunksModel`, `templatesModel`). Every write happens on the UI thread — either direct from `AppBridge::refresh()` (same thread as the 50 ms QTimer) or marshalled via `QMetaObject::invokeMethod(Qt::QueuedConnection)` for `MonitorLog`'s worker-thread callback.

Commits (chronological):
- `f639903` Step 4a — `JobsModel`. Roles: `jobId`, `name`, `slug`, `state`, `progress`, `totalChunks`, `doneChunks`, `failedChunks`, `renderingChunks`, `createdAt`, `priority`. `name` and `slug` both return `manifest.job_id` until Phase 4 decides if synthesizing a friendlier name is worth it. Fast-path `dataChanged` when IDs + order match; full reset otherwise.
- `5eb136f` Step 4b — `NodesModel` via `PeerManager::getPeerSnapshot()` (mutex-guarded inside PeerManager). Roles: `nodeId`, `hostname`, `isLeader`, `isActive`, `isAlive`, `agentHealth`, `alertReason`, `tags`, `priority`, `lastSeen`, `isThisNode`, `endpoint`, `renderState`, `activeJob`. Snapshot excludes the local node — NodePanel will read "this node" from separate AppBridge properties (Phase 4).
- `3263e13` Step 4c — `LogModel`. Takes over `MonitorLog::setCallback` (overwriting MonitorApp's old UI-IPC push — that subsystem is scheduled for Phase 7 deletion anyway). Ring buffer sized at 5000 entries. Callback fires from arbitrary threads; every entry routes through `QMetaObject::invokeMethod(this, "appendOnUiThread", Qt::QueuedConnection, …)`. Roles: `timestamp`, `level`, `category`, `message`. `~LogModel` clears the callback first so pending queued events can't race teardown.
- `1540730` Step 4d — `ChunksModel`. AppBridge gains `currentJobId` (R/W/NOTIFY) + a 3 s `QTimer` that calls `MonitorApp::getChunksForJob` while a job is selected. Roles: `chunkId`, `frameStart`, `frameEnd`, `state`, `assignedNode`, `progress`, `assignedAt`, `completedAt`, `retryCount`. **Caveat:** on worker nodes `getChunksForJob` blocks for ~500 ms–1.5 s (sync HTTP to leader). Phase 5's frame grid may need to promote it to `postToLeaderAsync`.
- `b2daea3` Step 4e — `TemplatesModel` over `MonitorApp::cachedTemplates()`. Roles: `templateId`, `name`, `dcc` (= templateId today), `path` (OS-specific `cmd` exe path), `flagCount`, `isValid`, `validationError`, `isExample`. Rarely changes, so a structural equivalence short-circuit saves churn; otherwise full reset.

Not in Phase 3 scope:
- `MonitorLog::addCallback`/`removeCallback` — we sidestepped by overwriting the single existing slot. If a second subsystem ever needs log entries before the UI-IPC subsystem dies in Phase 7, this becomes a real refactor.
- Surgical insert/remove diff on `JobsModel`/`NodesModel`/`ChunksModel`. Current slow path is `beginResetModel`, which rebuilds delegates and drops ListView selection. Acceptable for Phase 3 milestone; Phase 4 panels may justify the proper LCS-style diff.
- **Milestone:** bind a bare `ListView` to each model and see rows appear/update/disappear when backend state changes.

### Phase 4 — Remaining panels (no JobDetail) · 2 weeks · **3 of 5 panels landed**

1. **Log panel** · `7bcfbc4` **done.** ListView over `appBridge.logModel`. Toolbar with Info/Warn/Error filter chips (delegate `height: 0` filter — upgrade to `QSortFilterProxyModel` if churn becomes measurable), autoscroll that pauses when the user scrolls away from the bottom, entry counter, Clear button. Monospaced four-column delegate (time / level / category / message).
2. **Node panel** · `a7cb77d` **done.** Top section: "This Node" static info (hostname, node id, GPU, CPU/RAM, tags, LEADER badge) + Stop/Resume button. Bottom section: peer ListView (status dot coloured by `agentHealth`, hostname, LEADER badge on leader, status line). Right-click on a peer opens a Menu with Unsuspend. AppBridge gained `thisNode*` Q_PROPERTYs and `toggleNodeActive` / `unsuspendNode` invokables.
3. **Job list panel** · `91abfc0` **done.** ListView (not TableView — simpler, same column alignment via fixed widths) over `appBridge.jobsModel`. Columns: checkbox / name / state (colour-coded) / progress (ProgressBar + "done/total") / created. Per-row right-click menu: Pause, Resume, Retry failed chunks, Requeue, Cancel, Archive, Delete — each goes through a new AppBridge invokable. "New Job…" button in toolbar calls `appBridge.requestSubmissionMode()`. Header select-all is tristate for display, two-state on click. AppBridge.setCurrentJobId now also calls `MonitorApp::selectJob` so HTTP handlers stay in sync.
4. **Farm Cleanup dialog** · `246f0a5` **stubbed.** Modal that explains why the action isn't live yet and points at Phase 7. Replaces the dead-code log-only menu handler. Real implementation (wrap `POST /api/farm/scan-cleanup` → `POST /api/farm/cleanup` with an async scan→preview→confirm pipeline) is grouped with Phase 7's other decommission work.
5. **Submission form** · `4baf35f` **done.** `src/ui/qml/panels/SubmissionForm.qml` is a standalone component (Phase 5 Loader-mounts the same file inside JobDetailPanel without changes) opened from File → New Job and the JobListPanel's "New Job…" button via a modal `Dialog` in Main.qml. Form: template ComboBox bound to `templatesModel`, job name, frame start/end/chunk size/priority, dynamic per-template flag rows from `templateById`, error banner, Submit/Cancel. AppBridge gained `templateById(id) → QVariantMap` (template + flag list as variant maps), `submitJob(...)` (validates → bakes manifest → leader: `dispatchManager.queueSubmission`; worker: `postToLeaderAsync POST /api/jobs` with marshal-back to UI thread), and `submissionSucceeded`/`submissionFailed` signals. Out-of-v1: file picker for type=file flags, live output-path preview, inline per-field validation hints — all Phase 5 polish.

### Phase 5 — JobDetail + frame grid · 1–1.5 weeks · **code complete, benchmark pending**

Commits (chronological):
- `5e8d76e` Step 6a — `src/ui/qml/panels/JobDetailPanel.qml` replaces the bottom-left placeholder. Three-state Loader switch keyed off a new `appBridge.submissionMode` Q_PROPERTY (`submission` wins, else `empty` when no `currentJobId`, else `detail`). SubmissionForm moves from a modal Dialog (Phase 4 scaffold) to inline mount per the original plan. `AppBridge::requestSubmissionMode` now just flips our flag; `MonitorApp::shouldEnterSubmission` is dead code pending Phase 7.
- `32f30df` Step 6b — Detail-mode header: job name + state badge (colour per active/paused/cancelled/completed), progress bar + `done/total`, badge row (rendering / failed / priority / created-at), action row (Pause/Resume contextual, Retry failed when `failedChunks > 0`, Requeue, Cancel, Delete). Backed by a `QVariantMap currentJob` Q_PROPERTY that re-emits on every 20 Hz jobs refresh so the header stays live.
- `cedea3e` Step 6c — `src/ui/painters/frame_grid.{h,cpp}` — `QQuickPaintedItem` subclass. Q_PROPERTY: `ChunksModel* model`, `frameStart`, `frameEnd`, `cellSize`. Paint algorithm: pass 1 fills every frame cell with unclaimed colour; pass 2 overpaints each chunk's range in the chunk's colour, then overpaints `CompletedFramesRole` in green. `update()` only fires on model signals (`dataChanged` / `rowsInserted` / `rowsRemoved` / `modelReset`) or property changes — no timer, matching the plan's "zero GPU/CPU at idle" rule. Registered via `qmlRegisterType<MR::FrameGrid>("MinRenderUi", 1, 0, "FrameGrid")` in `main_qt.cpp`. Colours hardcoded; Theme.qml will replace them in Phase 6. Added `CompletedFramesRole` to ChunksModel exposing `ChunkRow::completed_frames` as a QVariantList for per-frame override rendering.
- `997e32d` Step 6d — JobDetailPanel Detail-mode content area is now a vertical SplitView: FrameGrid on top (preferredHeight 200), chunk ListView below with columns state / frames / node / progress / retries.
- `5b0ca98` Step 6e — chunk right-click Menu with two actions: Reassign (calls `appBridge.reassignChunk(id, "")` — empty target = dispatcher picks) and Submit as separate job (calls `appBridge.resubmitChunkAsJob` and switches `currentJobId` to the returned slug). Job-level actions (Retry failed / Requeue / Cancel / Delete) already live in the header from 6b.

Not yet done:
- **Frame grid benchmark** against 10 k frames. The v1 paint is two linear passes of `QPainter::fillRect` — should be OK under 5 ms but untested at scale. If it's slow, the plan calls for escalating to `QQuickItem::updatePaintNode` returning a `QSGGeometryNode` of batched quads.
- Node-picker submenu on the Reassign action (right now it hands "" to let dispatcher choose — good default, but users with a specific target node in mind have to work around it).
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
- **Strip RNDR dual-mode code.** User no longer runs in dual-mode; drop the whole subsystem in one atomic change. Surface area (from `git grep -il rndr` at time of writing):
  - `src/monitor/rndr_supervisor.{h,cpp}` — delete outright
  - `src/monitor/monitor_app.{h,cpp}` — remove `m_rndrSupervisor`, `rndrSupervisor()` accessor, every `m_rndrSupervisor.…` call in `init`/`update`/`shutdown`
  - `src/core/config.h` — drop `rndr_dual_mode` field + its `to_json`/`from_json` lines
  - `src/core/http_server.cpp` — drop any RNDR status endpoints (`grep rndr` for exact lines)
  - `CMakeLists.txt` — drop `src/monitor/rndr_supervisor.cpp` from `MINRENDER_CORE_SOURCES`
  - `docs/qt-port-plan.md` — remove historical RNDR mentions once the code is gone
  - (The QML / AppBridge surface was stripped already in Phase 2 — `rndrDualMode` Q_PROPERTY and its SettingsPanel entry are gone; only the `rndr_dual_mode` config field remains so existing `config.json` files on disk still parse.)
- **Drop `Config::font_scale`.** Qt's DPI scaling replaces the ImGui-era font-scale hack; the field should go with the UI toggle (also already removed in Phase 2).
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

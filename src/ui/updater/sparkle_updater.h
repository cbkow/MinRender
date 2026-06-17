// Sparkle / WinSparkle auto-updater seam — a C++-only interface over the
// platform updater (Sparkle 2's SPUStandardUpdaterController on macOS,
// WinSparkle on Windows). Both drive the standard update UI: check / download
// progress / release notes / install + relaunch.
//
// The macOS implementation lives in sparkle_updater_macos.mm (compiled only
// when Sparkle is vendored — CMake defines MINRENDER_HAVE_SPARKLE when
// external/Sparkle/Sparkle.framework exists). The Windows implementation lives
// in winsparkle_updater_windows.cpp (compiled only when WinSparkle is vendored
// — CMake defines MINRENDER_HAVE_WINSPARKLE when external/winsparkle exists).
// On every other configuration the calls compile to inline no-ops so callers
// can invoke them unconditionally.
//
// Only the `minrender` GUI target links the updater. The headless sidecar and
// mr-agent never check for updates: a render node must not interrupt a job with
// an update prompt, and a farm should roll forward deliberately, not per-node.
#pragma once

namespace MR {

#if defined(__APPLE__) && defined(MINRENDER_HAVE_SPARKLE)

// Construct + start the shared updater. Call once at launch after the UI is up.
// Honors the Info.plist SU* keys (SUFeedURL, SUEnableAutomaticChecks,
// SUScheduledCheckInterval, SUPublicEDKey). Idempotent.
void startSparkleUpdater();

// User-initiated "Check for Updates…" — shows Sparkle's standard UI.
void checkForUpdatesNow();

#elif defined(_WIN32) && defined(MINRENDER_HAVE_WINSPARKLE)

// Same seam, WinSparkle-backed (see winsparkle_updater_windows.cpp). Configures
// the feed / app-details / EdDSA key + automatic checks, then win_sparkle_init().
// Idempotent.
void startSparkleUpdater();

// User-initiated "Check for Updates…" — shows WinSparkle's standard UI.
void checkForUpdatesNow();

#else  // updater not vendored (dev builds / other platforms) — inline no-ops.

inline void startSparkleUpdater() {}
inline void checkForUpdatesNow() {}

#endif

} // namespace MR

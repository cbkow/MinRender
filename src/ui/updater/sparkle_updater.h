// Sparkle auto-updater seam — a C++-only interface over Sparkle 2's
// SPUStandardUpdaterController on macOS: check / download progress / release
// notes / install + relaunch.
//
// The macOS implementation lives in sparkle_updater_macos.mm (compiled only
// when Sparkle is vendored — CMake defines MINRENDER_HAVE_SPARKLE when
// external/Sparkle/Sparkle.framework exists). Windows has no self-updater:
// the Microsoft Store is the only Windows channel and delivers updates
// itself. On every other configuration the calls compile to inline no-ops so
// callers can invoke them unconditionally.
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

#else  // updater not vendored, or Windows — inline no-ops.

inline void startSparkleUpdater() {}
inline void checkForUpdatesNow() {}

#endif

// True when a real updater backend is compiled in. Drives whether the
// "Check for Updates…" menu item exists at all (Main.qml removes it when
// false): dev builds without Sparkle and every Windows build.
#if defined(__APPLE__) && defined(MINRENDER_HAVE_SPARKLE)
inline constexpr bool kUpdaterAvailable = true;
#else
inline constexpr bool kUpdaterAvailable = false;
#endif

} // namespace MR

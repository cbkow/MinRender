#pragma once

#include <QObject>
#include "ui/updater/sparkle_updater.h"

// Thin QML bridge to the Sparkle / WinSparkle updater. Exposed as the
// `appUpdater` context property so the About menu's "Check for Updates…" item
// can trigger a user-initiated check. When the updater isn't vendored (dev
// builds) or on the headless build, the underlying calls are inline no-ops.
class AppUpdater : public QObject
{
    Q_OBJECT
    // False when no updater backend is compiled in (dev builds, Store builds)
    // — the menu item binds enabled to this instead of no-opping on click.
    Q_PROPERTY(bool available READ available CONSTANT)
public:
    using QObject::QObject;

    bool available() const { return MR::kUpdaterAvailable; }

    Q_INVOKABLE void checkForUpdates() { MR::checkForUpdatesNow(); }
};

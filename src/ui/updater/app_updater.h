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
public:
    using QObject::QObject;

    Q_INVOKABLE void checkForUpdates() { MR::checkForUpdatesNow(); }
};

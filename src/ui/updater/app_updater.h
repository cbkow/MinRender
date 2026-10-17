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
    // Whether the About menu offers a manual update check — Main.qml removes
    // the item when false. False in Microsoft Store builds
    // (MINRENDER_STORE_BUILD: the Store owns updates and no updater is
    // linked) and in any build with no updater backend compiled in. The
    // direct-download WinSparkle build keeps the item.
    Q_PROPERTY(bool available READ available CONSTANT)
public:
    using QObject::QObject;

    bool available() const
    {
#if defined(MINRENDER_STORE_BUILD)
        return false;
#else
        return MR::kUpdaterAvailable;
#endif
    }

    Q_INVOKABLE void checkForUpdates() { MR::checkForUpdatesNow(); }
};

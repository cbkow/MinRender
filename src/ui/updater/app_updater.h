#pragma once

#include <QObject>
#include "ui/updater/sparkle_updater.h"

// Thin QML bridge to the Sparkle updater (macOS). Exposed as the
// `appUpdater` context property so the About menu's "Check for Updates…" item
// can trigger a user-initiated check. When the updater isn't vendored (dev
// builds) or on Windows (Store-delivered updates), the calls are no-ops and
// the menu item is removed.
class AppUpdater : public QObject
{
    Q_OBJECT
    // Whether the About menu offers a manual update check — Main.qml removes
    // the item when false: on Windows unconditionally (the Microsoft Store
    // owns updates) and in any build with no updater backend compiled in.
    Q_PROPERTY(bool available READ available CONSTANT)
public:
    using QObject::QObject;

    bool available() const { return MR::kUpdaterAvailable; }

    Q_INVOKABLE void checkForUpdates() { MR::checkForUpdatesNow(); }
};

#pragma once

#include <QColor>

namespace MR {

// OS accent color used to tint UI highlights. Windows pulls it from
// DwmGetColorizationColor (live with the user's theme); macOS will pull
// from NSColor.controlAccentColor in a Phase 8 .mm file; Linux/X11
// falls back to the Qt palette's Highlight role, which honors the
// active desktop theme. Called once at startup — live-change handling
// would require WM_SETTINGCHANGE plumbing, deferred to Phase 6.
QColor systemAccentColor();

} // namespace MR

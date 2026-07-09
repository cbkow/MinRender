pragma Singleton

import QtQuick

// Central palette + font-family + dimension singleton. Access from any QML file via
//   import MinRenderUi 1.0
//   ...
//   color: Theme.bg
//
// The visual language follows ufb's flat Fluent-ish style: squared
// corners, single cobalt-blue accent, minimal elevation, instant hover
// color swaps (no animation), Phosphor icons.
//
// Token groups:
//   - Surfaces: layered grey hierarchy (bg, surface, surfaceAlt, surfaceHover, toolbar)
//   - Borders / dividers
//   - Text (primary, muted, subtle, bright, inverted)
//   - Accent + state (success, warn, error, info)
//   - FrameGrid cell colours (used by JobDetailPanel via FrameGrid C++ painter)
//   - Fonts (Inter sans, JetBrainsMono, Phosphor icon font)
//   - Dimensions (rowHeight, toolStripHeight, padding, spacing, border)
//   - Icon sizes
QtObject {
    // --- Surfaces ---
    readonly property color bg:           "#161616"
    readonly property color bgAlt:        "#181818"
    readonly property color surface:      "#1a1a1a"
    readonly property color surfaceAlt:   "#1d1d1d"
    readonly property color surfaceHover: "#252525"
    readonly property color toolbar:      "#1f1f1f"
    readonly property color toolbarAlt:   "#262626"
    readonly property color border:       "#2a2a2a"
    readonly property color borderStrong: "#333333"
    readonly property color divider:      "#2a2a2a"
    // Modal layering: in a dark theme elevation goes lighter, so the
    // dialog card sits a step above the page (modalBg > bg), the scrim
    // drops the page back, and inputs recess into darker wells so
    // fields keep visible edges on the raised card.
    readonly property color modalBg:   "#202020"
    readonly property color inputWell: "#141414"
    readonly property color scrim:     Qt.rgba(0, 0, 0, 0.55)
    // Row-selection background — points at accentMuted so highlighted
    // rows read as a dimmed-cobalt fill (matches ufb).
    readonly property color selection:    accentMuted

    // --- Text ---
    readonly property color textPrimary:   "#dddddd"
    readonly property color textSecondary: "#888888"
    readonly property color textMuted:     "#666666"
    readonly property color textBright:    "#ffffff"
    readonly property color textInverted:  "#111111"

    // --- Semantic / accent ---
    // Accent is locked to a single cobalt blue rather than tracking the
    // OS accent (DwmGetColorizationColor on Windows / NSColor on macOS).
    // Predictable branding > per-user OS theme on a render-farm tool.
    readonly property color accent:         "#0189f1"
    readonly property color accentHover:    "#1b95f1"
    readonly property color accentMuted:    "#10395b"
    readonly property color accentSelected: accent
    readonly property color success:        "#4cb050"
    readonly property color warn:           "#f5a623"
    readonly property color error:          "#c04040"
    readonly property color info:           "#9cc9ff"

    // --- FrameGrid palette ---
    // The C++ FrameGrid painter binds to these properties; renaming
    // requires updating ui/painters/frame_grid.cpp.
    readonly property color frameBg:        "#0d0d0d"
    readonly property color frameUnclaimed: "#303030"
    readonly property color frameAssigned:  accent
    // Two-green progression for finished work:
    //   frameRendered (dark)  → frame rendered locally, sitting in staging
    //   frameCompleted (light) → chunk fully complete, files copied to dest
    readonly property color frameRendered:  "#2A8228"
    readonly property color frameCompleted: "#46C846"
    readonly property color frameFailed:    error

    // --- Fonts ---
    // Family names resolved by main_qt.cpp at startup. Falls back to a
    // generic string if the bundled .ttf didn't load.
    readonly property string fontFamily:     appBridge ? appBridge.interFamily    : "sans-serif"
    readonly property string monoFamily:     appBridge ? appBridge.monoFamily     : "monospace"
    // symbolsFamily is the legacy Material Symbols font kept for sites
    // that haven't migrated to Icon{} yet. New code should use Icon{}.
    readonly property string symbolsFamily:  appBridge ? appBridge.symbolsFamily  : "Segoe UI Symbol"
    readonly property string iconFamily:     appBridge ? appBridge.phosphorFamily : "Phosphor"

    // Font sizes in pixelSize units. QML's default "pointSize" is DPI-
    // dependent and unpredictable between platforms; we stick to pixels.
    readonly property int fontSizeTiny:   10
    readonly property int fontSizeSmall:  11
    readonly property int fontSizeBase:   12
    readonly property int fontSizeMedium: 13
    readonly property int fontSizeLarge:  14
    readonly property int fontSizeXL:     16

    // --- Icon sizes ---
    readonly property int iconSizeSmall:   12
    readonly property int iconSizeToolbar: 16
    readonly property int iconSizeMedium:  18
    readonly property int iconSizeLarge:   24

    // --- Dimensions ---
    readonly property int rowHeight:        24
    readonly property int rowHeightDense:   22
    readonly property int rowHeightTall:    28
    readonly property int toolStripHeight:  32
    readonly property int headerHeight:     28
    // Width of MrScrollBar's thumb. Delegates reserve this much on
    // their right edge so the bar overlays a padding gutter rather
    // than text/buttons when the AsNeeded policy reveals it.
    readonly property int scrollBarWidth:   8

    // --- Padding / spacing ---
    readonly property int paddingTight:  4
    readonly property int padding:       8
    readonly property int paddingLoose: 12

    readonly property int spacingTight:  2
    readonly property int spacing:       4
    readonly property int spacingLoose:  8

    // --- Borders / radii ---
    readonly property int borderWidth:  1
    readonly property int dividerWidth: 1
    // Squared corners everywhere by default. The legacy radiusSmall /
    // radiusBase / radiusLarge tokens stay for any component that wants
    // rounding (chips, badges); new flat controls should use radius: 0.
    readonly property int radius:       0
    readonly property int radiusSmall:  2
    readonly property int radiusBase:   3
    readonly property int radiusLarge:  6
    readonly property int radiusPill:  10
}

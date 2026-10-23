pragma Singleton

import QtQuick

// Central palette + font-family + dimension singleton. Access from any QML file via
//   import MinRenderUi 1.0
//   ...
//   color: Theme.bg
//
// The visual language is the family's flat look (one cobalt-blue accent,
// minimal elevation, Phosphor icons, Geist sans + Geist Mono) with the
// humanist pass minNotes took first (2026-10-21) and QCView followed
// (2026-10-23), adopted here 2026-10-23: three corner radii applied by
// surface class, a short hover / pressed colour ramp, popups that fade in.
//
// Token groups:
//   - Surfaces: layered grey hierarchy (bg, surface, surfaceAlt, surfaceHover, toolbar)
//   - Borders / dividers
//   - Text (primary, muted, subtle, bright, inverted)
//   - Accent + state (success, warn, error, info)
//   - FrameGrid cell colours (used by JobDetailPanel via FrameGrid C++ painter)
//   - Fonts (Geist sans, Geist Mono, Phosphor icon font)
//   - Dimensions (rowHeight, toolStripHeight, padding, spacing, border)
//   - Icon sizes
//   - Radii by surface class, motion durations
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
    // Edge affordances (QCView's tokens): a control that must read as
    // interactive at rest carries a subtle raised grey; hover steps up
    // to a clearer grey. Also the highlighted row of a themed menu.
    readonly property color affordanceIdle:  "#262626"
    readonly property color affordanceHover: "#323232"
    // A ramp's idle end is never "transparent": that is black at alpha
    // 0, so the colour animation would dip dark before it lightens.
    // This is surfaceHover at alpha 0 — the ramp only fades in.
    readonly property color hoverIdle: Qt.rgba(0x25 / 255, 0x25 / 255, 0x25 / 255, 0)
    // The same idea for a fill-on-hover danger control: error at alpha 0.
    readonly property color errorIdle: Qt.rgba(0xc0 / 255, 0x40 / 255, 0x40 / 255, 0)

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
    // generic string if the bundled .ttf didn't load. (AppBridge keeps
    // its historical accessor name interFamily; the face is Geist.)
    readonly property string fontFamily:     appBridge ? appBridge.interFamily    : "sans-serif"
    readonly property string monoFamily:     appBridge ? appBridge.monoFamily     : "monospace"
    // symbolsFamily is the legacy Material Symbols font kept for sites
    // that haven't migrated to Icon{} yet. New code should use Icon{}.
    readonly property string symbolsFamily:  appBridge ? appBridge.symbolsFamily  : "Segoe UI Symbol"
    readonly property string iconFamily:     appBridge ? appBridge.phosphorFamily : "Phosphor"

    // Font sizes in pixelSize units. QML's default "pointSize" is DPI-
    // dependent and unpredictable between platforms; we stick to pixels.
    // Geist Sans and Geist Mono share vertical metrics, so mono values
    // sit at the same size as their sans labels.
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
    // Three radii, by surface class (the humanist pass). Hairlines,
    // dividers, tab underlines and the frame grid stay square.
    readonly property int radiusSmall:  3    // buttons, chips, badges, wells, fields, menu rows, scrollbar thumbs, progress bars, tooltips
    readonly property int radius:       6    // popups, menus, dialogs, toasts, cards and their headers
    readonly property int radiusLarge: 10    // reserved (a switch's pill track) — so a fourth value is never invented ad hoc

    // --- Motion ---
    readonly property int hoverMs:  100      // the hover / pressed / selection colour ramp
    readonly property int popupMs:   80      // a popup's fade-in. No exit fade: it keeps a modal
                                             // popup alive for its length and eats the next click.
}

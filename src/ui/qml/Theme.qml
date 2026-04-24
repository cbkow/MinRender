pragma Singleton

import QtQuick

// Central palette + font-family singleton. Access from any QML file via
//   import MinRenderUi 1.0
//   ...
//   color: Theme.bg
//
// Colours cluster into four groups:
//   - Surface / background tones (bg, bgAlt, surface, surfaceAlt, border)
//   - Text tones (textPrimary, textSecondary, textMuted)
//   - Semantic states (accent, success, warn, error, info)
//   - FrameGrid cell colours (exposed so JobDetailPanel binds them
//     onto the C++ FrameGrid QQuickPaintedItem in the next commit)
//
// The accent tint tracks QPalette::Highlight / DwmGetColorizationColor
// when available (appBridge.accentColor); otherwise falls back to the
// Tokyo Night-ish blue the rest of the palette is tuned for.
QtObject {
    // --- Surfaces ---
    readonly property color bg:         "#161616"
    readonly property color bgAlt:      "#181818"
    readonly property color surface:    "#1a1a1a"
    readonly property color surfaceAlt: "#1e1e1e"
    readonly property color border:     "#2a2a2a"
    readonly property color selection:  "#2a3b5c"

    // --- Text ---
    readonly property color textPrimary:   "#e0e0e0"
    readonly property color textSecondary: "#999999"
    readonly property color textMuted:     "#666666"

    // --- Semantic ---
    readonly property color accent:  appBridge && appBridge.accentColor && appBridge.accentColor.a > 0
                                     ? appBridge.accentColor : "#7aa2f7"
    readonly property color success: "#9ece6a"
    readonly property color warn:    "#e0af68"
    readonly property color error:   "#f7768e"
    readonly property color info:    "#7aa2f7"

    // --- FrameGrid palette ---
    readonly property color frameBg:        "#0d0d0d"
    readonly property color frameUnclaimed: "#303030"
    readonly property color frameAssigned:  info
    readonly property color frameCompleted: success
    readonly property color frameFailed:    error

    // --- Fonts ---
    // Family names resolved by main_qt.cpp at startup. Falls back to a
    // generic string if the bundled .ttf didn't load (e.g. running from
    // a layout without the resources/fonts dir next to the exe).
    readonly property string fontFamily:        appBridge ? appBridge.interFamily   : "sans-serif"
    readonly property string monoFamily:        appBridge ? appBridge.monoFamily    : "monospace"
    readonly property string symbolsFamily:     appBridge ? appBridge.symbolsFamily : "Segoe UI Symbol"

    // Font sizes in pixelSize units. QML's default "pointSize" is DPI-
    // dependent and unpredictable between platforms; we stick to pixels.
    readonly property int fontSizeSmall:  10
    readonly property int fontSizeBase:   12
    readonly property int fontSizeMedium: 13
    readonly property int fontSizeLarge:  14
    readonly property int fontSizeXL:     16

    // --- Corner radii ---
    readonly property int radiusSmall: 2
    readonly property int radiusBase:  3
    readonly property int radiusLarge: 6
}

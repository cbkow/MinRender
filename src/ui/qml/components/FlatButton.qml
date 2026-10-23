import QtQuick
import QtQuick.Controls.Basic
import MinRenderUi 1.0

// Flat button matching the family's visual vocabulary (softly rounded
// since the humanist pass, 2026-10-23). Four variants: "default"
// (transparent idle, surfaceHover on hover), "primary" (accent-blue
// fill), "danger" (error-red fill), and "neutral" (subtle grey fill —
// for buttons that need visible weight on raised surfaces like MrDialog
// cards, where transparent-idle buttons disappear).
//
// Use:
//   FlatButton { text: "Save"; variant: "primary"; onClicked: ... }
//   FlatButton { iconName: "x"; tooltipText: qsTr("Close") }
//   FlatButton { iconName: "trash"; text: "Delete"; variant: "danger" }
//
// Hover ramps the fill over Theme.hoverMs and a press steps one shade
// past hover: the button answers the finger, not just the cursor. The
// idle fill of the default variant is Theme.hoverIdle, the hover grey at
// alpha 0, so the ramp only ever fades in.
//
// Tooltips: set `tooltipText` and the button shows the family's flat
// tooltip on hover. Don't use the attached `ToolTip.text/visible`
// pattern — that draws Fusion's tooltip, which doesn't match.
Button {
    id: root

    property string variant: "default"        // "default" | "primary" | "danger" | "neutral"
    property string iconName: ""              // Phosphor icon slug
    property int    iconSize: Theme.iconSizeToolbar
    property color  iconColor: "transparent"  // transparent => track text color
    property string tooltipText: ""
    // Corner rounding: the control radius for every variant. A caller
    // overrides it only for a cell that must butt flush against its
    // neighbours.
    property int    cornerRadius: Theme.radiusSmall

    // When true, the checked state draws no accent border and no fill —
    // only the text/icon color shifts via checkedColor. Use for filter
    // chips that should sit visually flat with the surrounding toolstrip
    // (e.g. log-level filters). Default behaviour is ufb's accent border.
    property bool   subtleChecked: false
    property color  checkedColor: Theme.textBright   // foreground when checked && subtleChecked
    property color  uncheckedColor: Theme.textMuted  // foreground when !checked && subtleChecked

    // Internal palette derived from variant + state.
    QtObject {
        id: pal
        readonly property color bgIdle: {
            if (!root.enabled) {
                // Disabled primary/danger get a muted fill rather than the
                // saturated brand colour — otherwise the dimmed text reads
                // as light-grey-on-blue/red and looks broken. accentMuted
                // is the same blue at much lower lightness, so the button
                // still reads as "blue action, currently unavailable".
                if (root.variant === "primary") return Theme.accentMuted
                if (root.variant === "danger")  return Qt.darker(Theme.error, 2.2)
                if (root.variant === "neutral") return Theme.toolbarAlt
                return Theme.hoverIdle
            }
            if (root.variant === "primary") return Theme.accent
            if (root.variant === "danger")  return Theme.error
            // Neutral needs real distance from the surfaces it sits on
            // (modalBg #202020 / toolbar #1f1f1f) — toolbarAlt-level
            // greys read as "no background" there.
            if (root.variant === "neutral") return Theme.borderStrong
            return Theme.hoverIdle
        }
        readonly property color bgHover:
            root.variant === "primary" ? Theme.accentHover
          : root.variant === "danger"  ? Qt.lighter(Theme.error, 1.15)
          : root.variant === "neutral" ? Qt.lighter(Theme.borderStrong, 1.25)
                                       : Theme.surfaceHover
        // Pressed: one step past hover.
        readonly property color bgPressed:
            root.variant === "primary" || root.variant === "danger" ? Qt.darker(bgHover, 1.15)
          : root.variant === "neutral" ? Qt.lighter(Theme.borderStrong, 1.45)
                                       : Theme.affordanceHover
        readonly property color fg: {
            if (!root.enabled) {
                // Disabled text uses textSecondary (#888) instead of
                // textMuted (#666) so it stays legible on the muted
                // primary/danger fills above and on Theme.bg for default.
                return Theme.textSecondary
            }
            if (root.subtleChecked)
                return root.checked ? root.checkedColor : root.uncheckedColor
            if (root.variant === "default") return Theme.textPrimary
            return Theme.textBright
        }
    }

    implicitHeight: Theme.toolStripHeight
    leftPadding:  rowContent.implicitWidth > 0 ? Theme.padding : 0
    rightPadding: rowContent.implicitWidth > 0 ? Theme.padding : 0
    topPadding:    0
    bottomPadding: 0

    background: Rectangle {
        radius: root.cornerRadius
        color: root.down ? pal.bgPressed
             : root.hovered ? pal.bgHover : pal.bgIdle
        Behavior on color { ColorAnimation { duration: Theme.hoverMs } }
        // Checked default-variant: accent border, no fill change. Matches
        // ufb exactly — keeps the toolstrip dark and uniform, with the
        // accent rule signalling state. subtleChecked opts out entirely
        // so a filter chip can stay perfectly flat.
        border.color: !root.subtleChecked
                       && root.checked
                       && root.variant === "default"
                       ? Theme.accent
                       : "transparent"
        border.width: !root.subtleChecked && root.checked ? Theme.borderWidth : 0
    }

    contentItem: Row {
        id: rowContent
        spacing: 6

        Icon {
            visible: root.iconName.length > 0
            name: root.iconName
            size: root.iconSize
            color: root.iconColor.a > 0 ? root.iconColor : pal.fg
            anchors.verticalCenter: parent.verticalCenter
            Behavior on color { ColorAnimation { duration: Theme.hoverMs } }
        }
        Label {
            visible: root.text.length > 0
            text: root.text
            color: pal.fg
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeBase
            anchors.verticalCenter: parent.verticalCenter
            Behavior on color { ColorAnimation { duration: Theme.hoverMs } }
        }
    }

    FlatToolTip {
        visible: root.hovered && root.tooltipText.length > 0
        text:    root.tooltipText
    }
}

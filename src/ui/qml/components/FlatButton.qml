import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import MinRenderUi 1.0

// Flat squared-corner button matching ufb's visual vocabulary.
// Three variants: "default" (transparent idle, surfaceHover on hover),
// "primary" (accent-blue fill), "danger" (error-red fill).
//
// Use:
//   FlatButton { text: "Save"; variant: "primary"; onClicked: ... }
//   FlatButton { iconName: "x"; ToolTip.text: "Close" }
//   FlatButton { iconName: "trash"; text: "Delete"; variant: "danger" }
//
// Hover is an instant color swap (no Transition) — that snappy reactivity
// is what makes the UI feel flat and fluent rather than skeuomorphic.
Button {
    id: root

    property string variant: "default"        // "default" | "primary" | "danger"
    property string iconName: ""              // Phosphor icon slug
    property int    iconSize: Theme.iconSizeToolbar
    property color  iconColor: "transparent"  // transparent => track text color

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
                return "transparent"
            }
            if (root.variant === "primary") return Theme.accent
            if (root.variant === "danger")  return Theme.error
            return "transparent"
        }
        readonly property color bgHover:
            root.variant === "primary" ? Theme.accentHover
          : root.variant === "danger"  ? Qt.lighter(Theme.error, 1.15)
                                       : Theme.surfaceHover
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
        radius: Theme.radius
        color: root.hovered ? pal.bgHover : pal.bgIdle
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
        leftPadding:  root.iconName.length > 0 && root.text.length > 0 ? 0 : 0

        Icon {
            visible: root.iconName.length > 0
            name: root.iconName
            size: root.iconSize
            color: root.iconColor.a > 0 ? root.iconColor : pal.fg
            anchors.verticalCenter: parent.verticalCenter
        }
        Label {
            visible: root.text.length > 0
            text: root.text
            color: pal.fg
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeBase
            anchors.verticalCenter: parent.verticalCenter
        }
    }
}

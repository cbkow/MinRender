import QtQuick
import QtQuick.Controls.Basic
import MinRenderUi 1.0

// The family's flat tooltip — opaque dark well, softly rounded corners,
// single-line subtle border, sans at fontSizeSmall. Fades in over
// Theme.popupMs.
//
// Use as a child of a hover target:
//
//   MouseArea {
//       id: hover
//       hoverEnabled: true
//       FlatToolTip { visible: hover.containsMouse; text: "..." }
//   }
//
// FlatButton has this tooltip baked in via its `tooltipText` property;
// FlatToolTip is the freestanding form for labels, grid cells or any
// custom Item that wants the same look. Prefer it over the attached
// `ToolTip.text` pattern, which draws Fusion's tooltip.
ToolTip {
    id: root

    delay: 600
    timeout: 5000

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.popupMs }
    }

    background: Rectangle {
        color:  Theme.inputWell
        border.color: Theme.border
        border.width: Theme.borderWidth
        radius: Theme.radiusSmall
    }
    contentItem: Text {
        text:  root.text
        color: Theme.textPrimary
        font.family:    Theme.fontFamily
        font.pixelSize: Theme.fontSizeSmall
        wrapMode: Text.WordWrap
    }
}

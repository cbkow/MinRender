import QtQuick
import QtQuick.Controls
import MinRenderUi 1.0

// Project-themed context Menu (the family's, from QCView). Its rows are
// ThemedMenuItem — declare that type inline too, never a plain MenuItem,
// which Fusion would draw with its own padding and the OS palette.
// Rounded like the other popups, the rows rounded like buttons, the row
// fill ramping over Theme.hoverMs, the menu fading in over Theme.popupMs.
//
// Use exactly like Menu:
//
//   ThemedMenu {
//       id: rowMenu
//       ThemedMenuItem { text: qsTr("Pause"); onTriggered: ... }
//       ThemedMenuSeparator {}
//       ThemedMenuItem { text: qsTr("Delete"); onTriggered: ... }
//   }
//   ... rowMenu.popup()
Menu {
    id: themedMenu
    padding: 3
    overlap: 1

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.popupMs }
    }

    background: Rectangle {
        implicitWidth:  200
        implicitHeight: 24
        color:        Theme.surface
        border.color: Theme.border
        border.width: Theme.borderWidth
        radius:       Theme.radius
    }

    delegate: ThemedMenuItem { }

    // The palette still reaches anything the delegate does not draw (a
    // plain MenuItem a caller drops in, a stock MenuSeparator).
    palette.window:           Theme.surface
    palette.windowText:       Theme.textPrimary
    palette.base:             Theme.surface
    palette.text:             Theme.textPrimary
    palette.button:           Theme.surface
    palette.buttonText:       Theme.textPrimary
    palette.highlight:        Theme.affordanceHover
    palette.highlightedText:  Theme.textBright
    palette.mid:              Theme.border
    palette.midlight:         Theme.surfaceHover
    palette.dark:             Theme.bg
    palette.shadow:           Theme.bg
    palette.disabled.text:       Theme.textMuted
    palette.disabled.windowText: Theme.textMuted
    palette.disabled.buttonText: Theme.textMuted
}

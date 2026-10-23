import QtQuick
import QtQuick.Controls
import MinRenderUi 1.0

// House-styled modal dialog: raised card, dark scrim, PanelHeader-style
// title strip. Layering rationale lives with the Theme.modalBg tokens.
// Callers keep setting size, closePolicy, and content as usual:
//
//   MrDialog {
//       title: qsTr("New Job")
//       width: 640
//       ...content...
//   }
Dialog {
    id: root

    modal: true
    anchors.centerIn: parent
    padding: 0

    // TextField/SpinBox/ComboBox derive their fill from palette.base;
    // recess them below the card so fields keep visible edges.
    palette.base: Theme.inputWell
    palette.window: Theme.modalBg

    Overlay.modal: Rectangle { color: Theme.scrim }

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.popupMs }
    }

    background: Rectangle {
        color: Theme.modalBg
        border.color: Theme.borderStrong
        border.width: Theme.borderWidth
        radius: Theme.radius
    }

    header: Rectangle {
        implicitHeight: Theme.toolStripHeight
        color: Theme.toolbarAlt
        // The strip follows the card's rounded top; its bottom edge is
        // the divider, square.
        topLeftRadius:  Theme.radius
        topRightRadius: Theme.radius

        Label {
            anchors.left: parent.left
            anchors.leftMargin: Theme.padding + 4
            anchors.verticalCenter: parent.verticalCenter
            text: root.title
            color: Theme.textBright
            font.family: Theme.fontFamily
            font.bold: true
            font.pixelSize: Theme.fontSizeBase
        }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: Theme.dividerWidth
            color: Theme.divider
        }
    }
}

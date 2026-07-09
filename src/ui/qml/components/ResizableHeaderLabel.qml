import QtQuick
import QtQuick.Controls
import MinRenderUi 1.0

// Column-header label with a drag grip on its right edge. The parent
// owns the width (typically an int backed by Settings so the layout
// persists) and applies it in the resizeTo handler:
//
//   ResizableHeaderLabel {
//       text: qsTr("State")
//       width: colSettings.state
//       onResizeTo: (w) => colSettings.state = w
//   }
//
// Double-clicking the grip restores defaultWidth when one is set.
Item {
    id: root

    property alias text: label.text
    property alias pixelSize: label.font.pixelSize
    property int minWidth: 40
    property int defaultWidth: 0

    signal resizeTo(int newWidth)

    height: parent ? parent.height : label.implicitHeight

    Label {
        id: label
        anchors.left: parent.left
        anchors.right: grip.left
        anchors.verticalCenter: parent.verticalCenter
        color: Theme.textSecondary
        font.pixelSize: Theme.fontSizeBase
        elide: Text.ElideRight
    }

    MouseArea {
        id: grip
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 8
        cursorShape: Qt.SplitHCursor
        preventStealing: true
        hoverEnabled: true

        // Incremental drag: the grip translates with the edge it moves,
        // so after each applied resize the pointer is back at lastX —
        // no global coordinate mapping needed.
        property real lastX: 0
        onPressed: (mouse) => lastX = mouse.x
        onPositionChanged: (mouse) => {
            if (!pressed)
                return
            const w = Math.max(root.minWidth,
                               Math.round(root.width + (mouse.x - lastX)))
            if (w !== root.width)
                root.resizeTo(w)
        }
        onDoubleClicked: {
            if (root.defaultWidth > 0)
                root.resizeTo(root.defaultWidth)
        }

        // Column divider — faintly visible at rest so the grab target
        // is discoverable, accent while hovered/dragging.
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.topMargin: 5
            anchors.bottomMargin: 5
            width: 1
            color: grip.containsMouse || grip.pressed ? Theme.accent : Theme.textMuted
            opacity: grip.containsMouse || grip.pressed ? 1.0 : 0.45
        }
    }
}

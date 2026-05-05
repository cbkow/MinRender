import QtQuick
import QtQuick.Layouts
import MinRenderUi 1.0

// 32px-tall horizontal action bar. Default-property delegate forwards
// child elements into the inner RowLayout so call sites read naturally:
//
//   ToolStrip {
//       FlatButton { iconName: "plus"; text: qsTr("Add") }
//       FlatButton { iconName: "trash" }
//       Item { Layout.fillWidth: true }     // spacer pushes next button right
//       FlatButton { iconName: "gear-six" }
//   }
//
// spacing: 0 by default — toolstrip buttons butt up against each other
// for a continuous Fluent-style strip. Insert an Item with fillWidth
// to break the strip into left/right groups.
Rectangle {
    id: root

    property bool showBottomDivider: true
    property alias spacing: layout.spacing
    default property alias content: layout.children

    implicitHeight: Theme.toolStripHeight
    color: Theme.toolbar

    Rectangle {
        visible: root.showBottomDivider
        anchors.left:   parent.left
        anchors.right:  parent.right
        anchors.bottom: parent.bottom
        height: Theme.dividerWidth
        color: Theme.divider
    }

    RowLayout {
        id: layout
        anchors.fill: parent
        spacing: 0
    }
}

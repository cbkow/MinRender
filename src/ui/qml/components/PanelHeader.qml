import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MinRenderUi 1.0

// Consistent header bar across panels. Title on the left, optional
// subtitle (typically a count like "· 12"), and a slot on the right
// for Buttons / ToolButtons / Labels. Use like:
//
//   PanelHeader {
//       title: qsTr("Jobs")
//       subtitle: "· " + jobList.count
//       Button { text: qsTr("New Job…"); onClicked: … }
//   }
Rectangle {
    id: root

    property alias title:    titleLabel.text
    property string subtitle: ""
    default property alias trailingContent: trailing.children

    implicitHeight: 32
    color: Theme.surface

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 6
        anchors.rightMargin: 6
        spacing: 6

        Label {
            id: titleLabel
            color: Theme.textPrimary
            font.bold: true
            font.pixelSize: Theme.fontSizeBase
        }
        Label {
            visible: root.subtitle.length > 0
            text: root.subtitle
            color: Theme.textMuted
            font.pixelSize: Theme.fontSizeBase
        }
        Item { Layout.fillWidth: true }
        RowLayout {
            id: trailing
            spacing: 4
        }
    }
}

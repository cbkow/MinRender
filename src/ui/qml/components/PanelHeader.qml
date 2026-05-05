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

    implicitHeight: Theme.toolStripHeight
    color: Theme.toolbar

    // Bottom divider — single 1px rule that separates the header from
    // panel content. Same idiom as ToolStrip; gives the flat toolbar
    // a crisp edge instead of relying on a colour-ramp seam.
    Rectangle {
        anchors.left:   parent.left
        anchors.right:  parent.right
        anchors.bottom: parent.bottom
        height: Theme.dividerWidth
        color: Theme.divider
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.padding
        anchors.rightMargin: Theme.spacingTight
        spacing: Theme.spacing

        Label {
            id: titleLabel
            color: Theme.textBright
            font.family: Theme.fontFamily
            font.bold: true
            font.pixelSize: Theme.fontSizeBase
        }
        Label {
            visible: root.subtitle.length > 0
            text: root.subtitle
            color: Theme.textMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeBase
        }
        Item { Layout.fillWidth: true }
        RowLayout {
            id: trailing
            spacing: 0
        }
    }
}

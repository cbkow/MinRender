import QtQuick
import QtQuick.Controls
import MinRenderUi 1.0

// A small coloured pill used for state labels (JobListPanel state
// column, JobDetailPanel header, NodePanel LEADER indicator, etc.).
// Defaults to the accent tint with dark text so it reads on any theme
// colour; set `fillColor` / `textColor` to override.
Rectangle {
    id: root

    property string text: ""
    property color  fillColor: Theme.accent
    property color  textColor: Theme.bg
    property int    textSize:  Theme.fontSizeSmall

    color: fillColor
    radius: Theme.radiusBase
    implicitWidth: label.implicitWidth + 10
    implicitHeight: label.implicitHeight + 4

    Label {
        id: label
        anchors.centerIn: parent
        text: root.text.toUpperCase()
        color: root.textColor
        font.pixelSize: root.textSize
        font.bold: true
    }
}

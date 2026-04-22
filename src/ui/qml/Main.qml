import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: window

    width: 1440
    height: 900
    visible: true
    title: qsTr("MinRender Monitor")

    // Phase 0 placeholder. The real menu bar, SplitView layout, panels,
    // tray, and AppBridge wiring land in Phase 1 and Phase 2.
    // See docs/qt-port-plan.md.

    ColumnLayout {
        anchors.centerIn: parent
        spacing: 12

        Label {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("MinRender — Qt 6 port")
            font.pixelSize: 22
            font.bold: true
        }

        Label {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Phase 0 scaffold. Empty window only. No backend wired.")
            opacity: 0.7
        }
    }
}

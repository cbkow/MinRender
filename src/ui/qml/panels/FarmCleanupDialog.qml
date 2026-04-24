import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Stub. Full implementation requires AppBridge to wrap the
// POST /api/farm/scan-cleanup → POST /api/farm/cleanup async pair
// (the leader-side HTTP endpoints) — Phase 7 work, scheduled
// alongside the other decommission/installer items.
//
// For Phase 4 the menu wires here so File → Farm Cleanup… does
// something visible instead of logging silently to stderr; the
// dialog itself just explains why the action is unavailable yet.
Dialog {
    id: root
    title: qsTr("Farm Cleanup")
    modal: true
    anchors.centerIn: parent
    width: 480
    standardButtons: Dialog.Close

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        Label {
            Layout.fillWidth: true
            text: qsTr("Farm Cleanup is not yet wired in this build.")
            font.bold: true
            wrapMode: Text.WordWrap
        }

        Label {
            Layout.fillWidth: true
            text: qsTr("The leader-side endpoints exist (POST /api/farm/scan-cleanup, "
                       + "POST /api/farm/cleanup), but the AppBridge wrapper that "
                       + "orchestrates scan → preview → confirm hasn't been built. "
                       + "It's tracked in docs/qt-port-plan.md as a Phase 7 item "
                       + "alongside the other decommission work.")
            color: "#999"
            font.pixelSize: 11
            wrapMode: Text.WordWrap
        }

        Label {
            Layout.fillWidth: true
            text: qsTr("Until then, run cleanup directly against the leader's HTTP API "
                       + "or wait for the next release.")
            color: "#999"
            font.pixelSize: 11
            wrapMode: Text.WordWrap
        }
    }
}

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

Item {
    id: root

    implicitWidth: 560
    implicitHeight: contentColumn.implicitHeight + footer.implicitHeight + 48

    signal accepted
    signal rejected

    FolderDialog {
        id: folderPicker
        title: qsTr("Select sync root")
        onAccepted: {
            const path = selectedFolder.toString().replace(/^file:\/{2,3}/, "")
            appBridge.syncRoot = path
            syncRootField.text = path
        }
    }

    ColumnLayout {
        id: contentColumn
        anchors {
            left: parent.left
            right: parent.right
            top: parent.top
            margins: 16
        }
        spacing: 14

        // --- Sync root ---
        Label { text: qsTr("Sync root"); font.bold: true }
        RowLayout {
            Layout.fillWidth: true
            TextField {
                id: syncRootField
                Layout.fillWidth: true
                text: appBridge.syncRoot
                onEditingFinished: appBridge.syncRoot = text
                placeholderText: qsTr("e.g. //server/share/MinRender")
            }
            Button {
                text: qsTr("Browse…")
                onClicked: {
                    if (syncRootField.text.length > 0)
                        folderPicker.currentFolder = "file:///" + syncRootField.text
                    folderPicker.open()
                }
            }
        }

        // --- Tags ---
        Label { text: qsTr("Tags (comma-separated)"); font.bold: true }
        TextField {
            id: tagsField
            Layout.fillWidth: true
            text: appBridge.tagsCsv
            onEditingFinished: appBridge.tagsCsv = text
            placeholderText: qsTr("gpu, rndr, workstation")
        }

        // --- Networking ---
        Label { text: qsTr("Networking"); font.bold: true }
        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 12
            rowSpacing: 8

            Label { text: qsTr("HTTP port") }
            SpinBox {
                id: httpPortField
                from: 1
                to: 65535
                editable: true
                value: appBridge.httpPort
                onValueModified: appBridge.httpPort = value
            }

            Label { text: qsTr("IP override") }
            TextField {
                id: ipField
                Layout.fillWidth: true
                text: appBridge.ipOverride
                onEditingFinished: appBridge.ipOverride = text
                placeholderText: qsTr("auto-detect")
            }

            CheckBox {
                id: udpField
                Layout.columnSpan: 2
                text: qsTr("UDP multicast heartbeat")
                checked: appBridge.udpEnabled
                onToggled: appBridge.udpEnabled = checked
            }

            Label {
                text: qsTr("UDP port")
                enabled: udpField.checked
            }
            SpinBox {
                id: udpPortField
                enabled: udpField.checked
                from: 1
                to: 65535
                editable: true
                value: appBridge.udpPort
                onValueModified: appBridge.udpPort = value
            }
        }

        // --- Render ---
        Label { text: qsTr("Rendering"); font.bold: true }
        CheckBox {
            text: qsTr("Enable staging")
            checked: appBridge.stagingEnabled
            onToggled: appBridge.stagingEnabled = checked
        }

        // --- UI ---
        Label { text: qsTr("User interface"); font.bold: true }
        CheckBox {
            text: qsTr("Show notifications")
            checked: appBridge.showNotifications
            onToggled: appBridge.showNotifications = checked
        }

        Item { Layout.fillHeight: true; Layout.minimumHeight: 8 }
    }

    // --- Save / Cancel ---
    RowLayout {
        id: footer
        anchors {
            left: parent.left
            right: parent.right
            bottom: parent.bottom
            margins: 16
        }
        spacing: 8

        Item { Layout.fillWidth: true }

        Button {
            text: qsTr("Cancel")
            onClicked: {
                appBridge.revertSettings()
                root.rejected()
            }
        }

        Button {
            text: qsTr("Save")
            highlighted: true
            onClicked: {
                appBridge.saveSettings()
                root.accepted()
            }
        }
    }
}

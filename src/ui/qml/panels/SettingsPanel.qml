import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import MinRenderUi 1.0

Item {
    id: root

    implicitWidth: 560
    implicitHeight: contentColumn.implicitHeight + footer.implicitHeight + 48

    signal accepted
    signal rejected

    FolderDialog {
        id: folderPicker
        title: qsTr("Select sync root")
        // QUrl::toLocalFile in the bridge handles UNC (\\server\share),
        // Windows drive letters, and percent-encoded characters. Doing
        // this by string surgery in QML drops the leading \\ on UNC.
        onAccepted: {
            const path = appBridge.urlToLocalPath(selectedFolder)
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
        RowLayout {
            Layout.fillWidth: true
            SectionHeader { text: qsTr("Sync root") }
            Item { Layout.fillWidth: true }
            Label {
                // Signal-driven: syncRootChanged refreshes the binding
                // when the user edits via the TextField or picker; this
                // evaluates syncRootIsValid each time.
                property bool ok: {
                    appBridge.syncRoot
                    return appBridge.syncRootIsValid()
                }
                visible: appBridge.syncRoot.length > 0
                text: ok ? qsTr("✓ path OK")
                         : qsTr("⚠ path not reachable")
                color: ok ? Theme.success : Theme.warn
                font.pixelSize: 11
            }
        }
        RowLayout {
            Layout.fillWidth: true
            TextField {
                id: syncRootField
                Layout.fillWidth: true
                text: appBridge.syncRoot
                onEditingFinished: appBridge.syncRoot = text
                placeholderText: qsTr("e.g. \\\\server\\share\\MinRender")
            }
            Button {
                text: qsTr("Browse…")
                onClicked: folderPicker.open()
            }
        }

        // --- Tags ---
        SectionHeader { text: qsTr("Tags (comma-separated)") }
        TextField {
            id: tagsField
            Layout.fillWidth: true
            text: appBridge.tagsCsv
            onEditingFinished: appBridge.tagsCsv = text
            placeholderText: qsTr("gpu, gpu-fast, workstation")
        }

        // --- Networking ---
        SectionHeader { text: qsTr("Networking") }
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
        SectionHeader { text: qsTr("Rendering") }
        CheckBox {
            text: qsTr("Enable staging")
            checked: appBridge.stagingEnabled
            onToggled: appBridge.stagingEnabled = checked
        }

        // --- UI ---
        SectionHeader { text: qsTr("User interface") }
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

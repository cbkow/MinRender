import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MinRenderUi 1.0

// Editor for cross-platform path mappings used by DispatchManager when a
// job is dispatched between OSes, and by AppBridge when a macOS submitter
// canonicalizes paths to Windows form. The dialog edits a local copy and
// only commits to AppBridge.setPathMappings on Save — Cancel discards.
MrDialog {
    id: root
    title: qsTr("Path Mappings")
    width: 820
    height: Math.min(600, parent ? parent.height - 80 : 560)

    // Local mutable copy. Each entry is {win, mac, lin, enabled, label}.
    // Reload from AppBridge on every open so re-opening discards any
    // unsaved edits from a prior session.
    property var mappings: []

    function reload() {
        try {
            mappings = JSON.parse(appBridge.pathMappingsJson())
        } catch (e) {
            mappings = []
        }
    }

    function addMapping() {
        // Replace the array (not push) so QML's reactivity fires.
        const next = mappings.slice()
        next.push({ win: "", mac: "", lin: "", enabled: true, label: "" })
        mappings = next
    }

    function removeMapping(index) {
        const next = mappings.slice()
        next.splice(index, 1)
        mappings = next
    }

    function updateField(index, field, value) {
        // In-place mutation is fine for editor-typed fields; the array
        // reference doesn't change, so QML doesn't need to re-render the
        // ListView (the per-row TextField already shows the latest text).
        mappings[index][field] = value
    }

    function save() {
        // Strip rows that are completely empty (user added a row, never
        // typed anything). A row with only a label is kept so they don't
        // lose the work-in-progress label by accident.
        const cleaned = []
        for (let i = 0; i < mappings.length; ++i) {
            const m = mappings[i]
            if (!m.win && !m.mac && !m.lin && !m.label) continue
            cleaned.push(m)
        }
        appBridge.setPathMappings(JSON.stringify(cleaned))
        root.accept()
    }

    onOpened: reload()

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 8

        Label {
            Layout.fillWidth: true
            text: qsTr("minRender stores paths in canonical Windows form. Mappings translate between Windows, macOS, and Linux roots when jobs cross OSes or when a macOS user submits to the Windows farm.")
            color: Theme.textMuted
            font.pixelSize: Theme.fontSizeSmall
            wrapMode: Text.WordWrap
        }

        // Header row
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Label {
                text: qsTr("On")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeSmall
                Layout.preferredWidth: 28
            }
            Label {
                text: qsTr("Label")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeSmall
                Layout.preferredWidth: 120
            }
            Label {
                text: qsTr("Windows")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeSmall
                Layout.fillWidth: true
            }
            Label {
                text: qsTr("macOS")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeSmall
                Layout.fillWidth: true
            }
            Label {
                text: qsTr("Linux")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeSmall
                Layout.fillWidth: true
            }
            Item { Layout.preferredWidth: 28 }
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth
            ScrollBar.vertical: MrScrollBar {}
            ScrollBar.horizontal: MrScrollBar { policy: ScrollBar.AlwaysOff }

            ColumnLayout {
                width: parent.width - 16
                spacing: 4

                Label {
                    visible: root.mappings.length === 0
                    text: qsTr("No mappings yet — click Add Mapping below.")
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSizeSmall
                }

                Repeater {
                    // Bind to length so adding/removing rows triggers a
                    // rebuild; binding to the array directly doesn't fire
                    // because we replace the reference but Repeater
                    // sometimes caches by index.
                    model: root.mappings.length

                    delegate: RowLayout {
                        Layout.fillWidth: true
                        spacing: 6

                        required property int index

                        CheckBox {
                            Layout.preferredWidth: 28
                            checked: root.mappings[index] ? root.mappings[index].enabled : false
                            onToggled: root.updateField(index, "enabled", checked)
                        }
                        TextField {
                            Layout.preferredWidth: 120
                            text: root.mappings[index] ? (root.mappings[index].label || "") : ""
                            placeholderText: qsTr("e.g. Render share")
                            onEditingFinished: root.updateField(index, "label", text)
                        }
                        TextField {
                            Layout.fillWidth: true
                            text: root.mappings[index] ? (root.mappings[index].win || "") : ""
                            placeholderText: qsTr("Z:\\\\renders")
                            onEditingFinished: root.updateField(index, "win", text)
                            font.family: Theme.monoFamily
                            font.pixelSize: Theme.fontSizeSmall
                        }
                        TextField {
                            Layout.fillWidth: true
                            text: root.mappings[index] ? (root.mappings[index].mac || "") : ""
                            placeholderText: qsTr("/Volumes/renders")
                            onEditingFinished: root.updateField(index, "mac", text)
                            font.family: Theme.monoFamily
                            font.pixelSize: Theme.fontSizeSmall
                        }
                        TextField {
                            Layout.fillWidth: true
                            text: root.mappings[index] ? (root.mappings[index].lin || "") : ""
                            placeholderText: qsTr("/mnt/renders")
                            onEditingFinished: root.updateField(index, "lin", text)
                            font.family: Theme.monoFamily
                            font.pixelSize: Theme.fontSizeSmall
                        }
                        FlatButton {
                            Layout.preferredWidth: Theme.toolStripHeight
                            iconName: "trash"
                            ToolTip.text: qsTr("Remove")
                            ToolTip.visible: hovered
                            ToolTip.delay: 500
                            onClicked: root.removeMapping(index)
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingLoose

            FlatButton {
                iconName: "plus"
                text: qsTr("Add Mapping")
                variant: "primary"
                onClicked: root.addMapping()
            }

            Item { Layout.fillWidth: true }

            FlatButton {
                text: qsTr("Cancel")
                onClicked: root.reject()
            }
            FlatButton {
                iconName: "check"
                text: qsTr("Save")
                variant: "primary"
                onClicked: root.save()
            }
        }
    }
}

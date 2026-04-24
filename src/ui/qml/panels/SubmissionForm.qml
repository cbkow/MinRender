import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import MinRenderUi 1.0

// Standalone submission form, Loader-mounted inside JobDetailPanel's
// Submission branch (Phase 5). submitted/cancelled/failed signals
// give the wrapper a clean lifecycle hook.
Item {
    id: root

    signal submitted(string jobId)
    signal cancelled
    signal failed(string reason)

    implicitWidth: 640
    implicitHeight: column.implicitHeight + 32

    // Currently selected template — refreshed when the picker changes.
    // Starts null; flag fields and frame defaults update once a template
    // is chosen.
    property var currentTemplate: null
    property var currentFlagValues: []
    property string currentTemplateId: ""

    function refreshTemplate(templateId) {
        currentTemplateId = templateId
        if (templateId.length === 0) {
            currentTemplate = null
            currentFlagValues = []
            return
        }
        const t = appBridge.templateById(templateId)
        currentTemplate = t
        if (t && t.flags) {
            const initial = []
            for (let i = 0; i < t.flags.length; ++i)
                initial.push(t.flags[i].value || "")
            currentFlagValues = initial
            frameStartField.value = t.frameStart
            frameEndField.value   = t.frameEnd
            chunkSizeField.value  = t.chunkSize
            priorityField.value   = t.priority
        } else {
            currentFlagValues = []
        }
    }

    // Listen for results from the bridge.
    Connections {
        target: appBridge
        function onSubmissionSucceeded(jobId) { root.submitted(jobId) }
        function onSubmissionFailed(reason)   { root.failed(reason); errorBanner.text = reason }
    }

    // Shared file picker for type=file flag rows. Each row sets the
    // pending-index + filter before opening; onAccepted writes back
    // to that row's flag value.
    FileDialog {
        id: flagFilePicker
        property int targetIndex: -1

        onAccepted: {
            if (targetIndex < 0) return
            const local = appBridge.urlToLocalPath(selectedFile)
            if (root.currentFlagValues && local.length > 0)
                root.currentFlagValues[targetIndex] = local
            // Force repeater rows to re-read their text property from
            // currentFlagValues. Reassigning the array triggers the
            // Repeater's model binding.
            const copy = root.currentFlagValues.slice()
            root.currentFlagValues = copy
            targetIndex = -1
        }
    }

    // Content lives in a ScrollView so templates with many flag fields,
    // long descriptions, or small panel heights don't push the Save /
    // Cancel footer off-screen. The footer is anchored to the panel
    // bottom so it stays pinned while the form scrolls.
    ScrollView {
        id: scrollArea
        anchors {
            left: parent.left
            right: parent.right
            top: parent.top
            bottom: footer.top
            leftMargin: 16
            // Extra right margin reserves space for the vertical scrollbar
            // so it doesn't overlay the TextFields when content is tall
            // enough to scroll. Matches the AlwaysOn policy below.
            rightMargin: 24
            topMargin: 16
            bottomMargin: 8
        }
        clip: true
        contentWidth: availableWidth
        ScrollBar.vertical.policy: ScrollBar.AlwaysOn
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        ColumnLayout {
            id: column
            width: scrollArea.availableWidth
            spacing: 12

        // --- Template picker ---
        SectionHeader { text: qsTr("Template") }
        ComboBox {
            id: templatePicker
            Layout.fillWidth: true
            model: appBridge.templatesModel
            textRole: "name"
            valueRole: "templateId"
            currentIndex: -1
            onActivated: root.refreshTemplate(currentValue || "")
        }

        // --- Job name ---
        SectionHeader { text: qsTr("Job name") }
        TextField {
            id: jobNameField
            Layout.fillWidth: true
            placeholderText: qsTr("e.g. shot_010_v003")
        }

        // --- Frame range / chunk / priority ---
        SectionHeader { text: qsTr("Range") }
        GridLayout {
            Layout.fillWidth: true
            columns: 4
            columnSpacing: 12
            rowSpacing: 4

            Label { text: qsTr("Start") }
            Label { text: qsTr("End") }
            Label { text: qsTr("Chunk") }
            Label { text: qsTr("Priority") }

            SpinBox { id: frameStartField; from: 0;   to: 1000000; editable: true; value: 1 }
            SpinBox { id: frameEndField;   from: 0;   to: 1000000; editable: true; value: 250 }
            SpinBox { id: chunkSizeField;  from: 1;   to: 10000;   editable: true; value: 1 }
            SpinBox { id: priorityField;   from: 1;   to: 999;     editable: true; value: 50 }
        }

        // --- Flags (dynamic) ---
        SectionHeader {
            text: qsTr("Flags")
            visible: currentTemplate && currentTemplate.flags && currentTemplate.flags.length > 0
        }

        Repeater {
            id: flagRepeater
            model: currentTemplate ? currentTemplate.flags : []

            delegate: ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                required property var modelData
                required property int index

                visible: modelData.editable === true || modelData.required === true

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Label {
                        text: (modelData.info && modelData.info.length > 0)
                              ? modelData.info
                              : (modelData.flag.length > 0 ? modelData.flag : qsTr("(positional)"))
                        color: modelData.required ? Theme.warn : Theme.textSecondary
                        font.pixelSize: 11
                        Layout.preferredWidth: 140
                    }

                    TextField {
                        id: flagInput
                        Layout.fillWidth: true
                        readOnly: !modelData.editable
                        text: (root.currentFlagValues && root.currentFlagValues[index] !== undefined)
                              ? root.currentFlagValues[index] : ""
                        onTextChanged: {
                            if (root.currentFlagValues
                                && root.currentFlagValues[index] !== text)
                            {
                                root.currentFlagValues[index] = text
                            }
                        }
                    }

                    // File picker for flags the template marks as type=file
                    // (e.g. Scene File in the C4D template). filter is
                    // turned into a Qt name-filter string: "Filter (*.ext)".
                    Button {
                        visible: modelData.type === "file"
                        text: qsTr("Browse…")
                        onClicked: {
                            flagFilePicker.targetIndex = index
                            flagFilePicker.nameFilters = modelData.filter
                                && modelData.filter.length > 0
                                ? [modelData.filter + " (*." + modelData.filter + ")",
                                   "All files (*)"]
                                : ["All files (*)"]
                            // Seed the picker near the current value's folder
                            // if one is set.
                            const cur = flagInput.text
                            if (cur && cur.length > 0) {
                                // Convert Windows paths to file URL for QML.
                                flagFilePicker.currentFile =
                                    "file:///" + cur.replace(/\\/g, "/")
                            }
                            flagFilePicker.open()
                        }
                    }
                }

                Label {
                    visible: modelData.help && modelData.help.length > 0
                    Layout.fillWidth: true
                    text: modelData.help
                    color: Theme.textMuted
                    font.pixelSize: 10
                    wrapMode: Text.WordWrap
                    leftPadding: 148
                }
            }
        }

            // --- Error banner (filled on submissionFailed) ---
            Rectangle {
                Layout.fillWidth: true
                visible: errorBanner.text.length > 0
                color: Qt.darker(Theme.error, 3.0)
                radius: Theme.radiusBase
                implicitHeight: errorBanner.implicitHeight + 12

                Label {
                    id: errorBanner
                    anchors.fill: parent
                    anchors.margins: 6
                    color: Theme.error
                    wrapMode: Text.WordWrap
                    font.pixelSize: Theme.fontSizeBase
                }
            }
        }
    }

    // --- Submit / Cancel (pinned footer) ---
    RowLayout {
        id: footer
        anchors {
            left: parent.left
            right: parent.right
            bottom: parent.bottom
            leftMargin: 16
            rightMargin: 16
            bottomMargin: 16
        }
        spacing: 8

        Item { Layout.fillWidth: true }

        Button {
            text: qsTr("Cancel")
            onClicked: root.cancelled()
        }

        Button {
            text: qsTr("Submit")
            highlighted: true
            enabled: jobNameField.text.length > 0 && currentTemplate !== null
            onClicked: {
                errorBanner.text = ""
                appBridge.submitJob(
                    currentTemplateId,
                    jobNameField.text,
                    currentFlagValues,
                    frameStartField.value,
                    frameEndField.value,
                    chunkSizeField.value,
                    priorityField.value)
            }
        }
    }
}

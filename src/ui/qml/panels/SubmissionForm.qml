import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Standalone submission form. Phase 5 mounts this inside JobDetailPanel's
// Empty/Submission branch via Loader; for now it lives inside a Dialog
// opened by the New Job menu / button. The submitted/cancelled signals
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

    ColumnLayout {
        id: column
        anchors {
            left: parent.left
            right: parent.right
            top: parent.top
            margins: 16
        }
        spacing: 12

        // --- Template picker ---
        Label { text: qsTr("Template"); font.bold: true }
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
        Label { text: qsTr("Job name"); font.bold: true }
        TextField {
            id: jobNameField
            Layout.fillWidth: true
            placeholderText: qsTr("e.g. shot_010_v003")
        }

        // --- Frame range / chunk / priority ---
        Label { text: qsTr("Range"); font.bold: true }
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
        Label {
            text: qsTr("Flags")
            font.bold: true
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
                        color: modelData.required ? "#e0af68" : "#bbb"
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
                }

                Label {
                    visible: modelData.help && modelData.help.length > 0
                    Layout.fillWidth: true
                    text: modelData.help
                    color: "#666"
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
            color: "#3a1f1f"
            radius: 3
            implicitHeight: errorBanner.implicitHeight + 12

            Label {
                id: errorBanner
                anchors.fill: parent
                anchors.margins: 6
                color: "#f7768e"
                wrapMode: Text.WordWrap
                font.pixelSize: 11
            }
        }

        // --- Submit / Cancel ---
        RowLayout {
            Layout.fillWidth: true
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
}

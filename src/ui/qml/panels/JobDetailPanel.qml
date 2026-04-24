import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Three-state panel: Empty (nothing selected), Submission (user is
// creating a new job), Detail (a real job is selected). submissionMode
// takes precedence; otherwise we show Empty when currentJobId is blank,
// Detail otherwise.
Item {
    id: root

    readonly property string mode: {
        if (appBridge.submissionMode)            return "submission"
        if (appBridge.currentJobId.length === 0) return "empty"
        return "detail"
    }

    Loader {
        anchors.fill: parent
        active: root.mode === "empty"
        sourceComponent: emptyComponent
    }

    Loader {
        anchors.fill: parent
        active: root.mode === "submission"
        sourceComponent: submissionComponent
    }

    Loader {
        anchors.fill: parent
        active: root.mode === "detail"
        sourceComponent: detailComponent
    }

    Component {
        id: emptyComponent
        Rectangle {
            color: "#1a1a1a"
            ColumnLayout {
                anchors.centerIn: parent
                spacing: 10
                Label {
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("No job selected")
                    color: "#888"
                    font.pixelSize: 14
                }
                Label {
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("Pick one in the Jobs list, or click New Job.")
                    color: "#666"
                    font.pixelSize: 11
                }
                Button {
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("New Job…")
                    onClicked: appBridge.submissionMode = true
                }
            }
        }
    }

    Component {
        id: submissionComponent
        Rectangle {
            color: "#141414"
            SubmissionForm {
                anchors.fill: parent
                onSubmitted: (jobId) => {
                    appBridge.submissionMode = false
                    appBridge.currentJobId = jobId
                }
                onCancelled: appBridge.submissionMode = false
                // failed: form's error banner shows the reason; panel
                // stays in submission mode so the user can fix and retry.
            }
        }
    }

    Component {
        id: detailComponent
        Rectangle {
            color: "#141414"
            ColumnLayout {
                anchors.centerIn: parent
                spacing: 6
                Label {
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("Job detail (header + frame grid in Step 6b-6d)")
                    color: "#888"
                    font.pixelSize: 13
                }
                Label {
                    Layout.alignment: Qt.AlignHCenter
                    text: appBridge.currentJobId
                    color: "#bbb"
                    font.family: "monospace"
                    font.pixelSize: 11
                }
            }
        }
    }
}

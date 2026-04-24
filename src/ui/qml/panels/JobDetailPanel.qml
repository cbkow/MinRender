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

            readonly property var job: appBridge.currentJob

            function stateColor(s) {
                switch (s) {
                case "active":    return "#9ece6a"
                case "paused":    return "#e0af68"
                case "cancelled": return "#f7768e"
                case "completed": return "#7aa2f7"
                default:          return "#888"
                }
            }

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                // --- Header ---
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: headerColumn.implicitHeight + 16
                    color: "#1a1a1a"

                    ColumnLayout {
                        id: headerColumn
                        anchors {
                            left: parent.left
                            right: parent.right
                            top: parent.top
                            margins: 10
                        }
                        spacing: 6

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Label {
                                text: job.name || appBridge.currentJobId
                                color: "#e0e0e0"
                                font.pixelSize: 14
                                font.bold: true
                                font.family: "monospace"
                                elide: Text.ElideMiddle
                                Layout.fillWidth: true
                            }
                            Rectangle {
                                color: stateColor(job.state || "")
                                radius: 3
                                implicitWidth: stateLabel.implicitWidth + 10
                                implicitHeight: 18
                                Label {
                                    id: stateLabel
                                    anchors.centerIn: parent
                                    text: (job.state || "—").toUpperCase()
                                    color: "#0f0f0f"
                                    font.pixelSize: 10
                                    font.bold: true
                                }
                            }
                        }

                        // Progress line
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            ProgressBar {
                                Layout.fillWidth: true
                                from: 0; to: 1
                                value: job.progress || 0
                            }
                            Label {
                                text: qsTr("%1 / %2")
                                    .arg(job.doneChunks || 0)
                                    .arg(job.totalChunks || 0)
                                color: "#aaa"
                                font.pixelSize: 11
                                font.family: "monospace"
                            }
                        }

                        // Badges row: rendering / failed / priority
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10

                            Label {
                                visible: (job.renderingChunks || 0) > 0
                                text: qsTr("● %1 rendering").arg(job.renderingChunks || 0)
                                color: "#7aa2f7"
                                font.pixelSize: 10
                            }
                            Label {
                                visible: (job.failedChunks || 0) > 0
                                text: qsTr("● %1 failed").arg(job.failedChunks || 0)
                                color: "#f7768e"
                                font.pixelSize: 10
                            }
                            Label {
                                text: qsTr("priority %1").arg(job.priority || 0)
                                color: "#888"
                                font.pixelSize: 10
                            }
                            Item { Layout.fillWidth: true }
                            Label {
                                visible: (job.createdAt || 0) > 0
                                text: Qt.formatDateTime(new Date(job.createdAt), "yyyy-MM-dd HH:mm")
                                color: "#666"
                                font.pixelSize: 10
                            }
                        }

                        // --- Actions row ---
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.topMargin: 4
                            spacing: 6

                            Button {
                                visible: job.state === "active"
                                text: qsTr("Pause")
                                onClicked: appBridge.pauseJob(appBridge.currentJobId)
                            }
                            Button {
                                visible: job.state === "paused"
                                text: qsTr("Resume")
                                onClicked: appBridge.resumeJob(appBridge.currentJobId)
                            }
                            Button {
                                text: qsTr("Retry failed")
                                enabled: (job.failedChunks || 0) > 0
                                onClicked: appBridge.retryFailedChunks(appBridge.currentJobId)
                            }
                            Button {
                                text: qsTr("Requeue")
                                onClicked: appBridge.requeueJob(appBridge.currentJobId)
                            }
                            Item { Layout.fillWidth: true }
                            Button {
                                text: qsTr("Cancel")
                                onClicked: appBridge.cancelJob(appBridge.currentJobId)
                            }
                            Button {
                                text: qsTr("Delete")
                                onClicked: appBridge.deleteJob(appBridge.currentJobId)
                            }
                        }
                    }
                }

                // --- Content area (frame grid + chunks — Step 6d) ---
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: "#141414"
                    Label {
                        anchors.centerIn: parent
                        text: qsTr("Frame grid lands in Step 6d")
                        color: "#555"
                        font.pixelSize: 11
                    }
                }
            }
        }
    }
}

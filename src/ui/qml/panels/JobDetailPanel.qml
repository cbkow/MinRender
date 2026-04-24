import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MinRenderUi 1.0   // FrameGrid (C++ QQuickPaintedItem) + Theme singleton

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
            color: Theme.surface
            ColumnLayout {
                anchors.centerIn: parent
                spacing: 10
                Label {
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("No job selected")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeLarge
                }
                Label {
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("Pick one in the Jobs list, or click New Job.")
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSizeBase
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
            color: Theme.bg
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
            id: detailRoot
            color: Theme.bg

            readonly property var job: appBridge.currentJob

            // Right-click menu on chunk rows. Lives at the panel level
            // so popup() coordinates make sense and the menu doesn't
            // get clipped by the ListView's clip:true.
            Menu {
                id: chunkMenu
                property var targetChunkId: 0
                property int targetFrameStart: 0
                property int targetFrameEnd: 0

                MenuItem {
                    text: qsTr("Reassign to another node")
                    onTriggered: appBridge.reassignChunk(chunkMenu.targetChunkId, "")
                }
                MenuItem {
                    text: qsTr("Submit as separate job")
                    onTriggered: {
                        const newId = appBridge.resubmitChunkAsJob(
                            appBridge.currentJobId,
                            chunkMenu.targetFrameStart,
                            chunkMenu.targetFrameEnd,
                            chunkMenu.targetFrameEnd - chunkMenu.targetFrameStart + 1)
                        if (newId.length > 0)
                            appBridge.currentJobId = newId
                    }
                }
            }

            function stateColor(s) {
                switch (s) {
                case "active":    return Theme.success
                case "paused":    return Theme.warn
                case "cancelled": return Theme.error
                case "completed": return Theme.info
                default:          return Theme.textSecondary
                }
            }

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                // --- Header ---
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: headerColumn.implicitHeight + 16
                    color: Theme.surface

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
                                color: Theme.textPrimary
                                font.pixelSize: Theme.fontSizeLarge
                                font.bold: true
                                font.family: Theme.monoFamily
                                elide: Text.ElideMiddle
                                Layout.fillWidth: true
                            }
                            Rectangle {
                                color: stateColor(job.state || "")
                                radius: Theme.radiusBase
                                implicitWidth: stateLabel.implicitWidth + 10
                                implicitHeight: 18
                                Label {
                                    id: stateLabel
                                    anchors.centerIn: parent
                                    text: (job.state || "—").toUpperCase()
                                    color: Theme.bg
                                    font.pixelSize: Theme.fontSizeSmall
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
                                color: Theme.textPrimary
                                font.pixelSize: Theme.fontSizeBase
                                font.family: Theme.monoFamily
                            }
                        }

                        // Badges row: rendering / failed / priority
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10

                            Label {
                                visible: (job.renderingChunks || 0) > 0
                                text: qsTr("● %1 rendering").arg(job.renderingChunks || 0)
                                color: Theme.info
                                font.pixelSize: Theme.fontSizeSmall
                            }
                            Label {
                                visible: (job.failedChunks || 0) > 0
                                text: qsTr("● %1 failed").arg(job.failedChunks || 0)
                                color: Theme.error
                                font.pixelSize: Theme.fontSizeSmall
                            }
                            Label {
                                text: qsTr("priority %1").arg(job.priority || 0)
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fontSizeSmall
                            }
                            Item { Layout.fillWidth: true }
                            Label {
                                visible: (job.createdAt || 0) > 0
                                text: Qt.formatDateTime(new Date(job.createdAt), "yyyy-MM-dd HH:mm")
                                color: Theme.textMuted
                                font.pixelSize: Theme.fontSizeSmall
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

                // --- Content area (frame grid + chunk list) ---
                SplitView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    orientation: Qt.Vertical

                    // Frame grid — one cell per frame, coloured by state.
                    Rectangle {
                        color: Theme.frameBg
                        SplitView.preferredHeight: 200
                        SplitView.minimumHeight: 60

                        FrameGrid {
                            anchors.fill: parent
                            anchors.margins: 4
                            model: appBridge.chunksModel
                            frameStart: job.frameStart || 0
                            frameEnd:   job.frameEnd   || 0
                            cellSize:   10
                            bgColor:        Theme.frameBg
                            unclaimedColor: Theme.frameUnclaimed
                            assignedColor:  Theme.frameAssigned
                            completedColor: Theme.frameCompleted
                            failedColor:    Theme.frameFailed
                        }
                    }

                    // Chunk list — scrollable, read-only for now.
                    // Step 6e adds the right-click context menu.
                    ColumnLayout {
                        SplitView.fillHeight: true
                        spacing: 0

                        // Column header row
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 22
                            color: Theme.border
                            Row {
                                anchors.fill: parent
                                anchors.leftMargin: 8
                                anchors.rightMargin: 8
                                spacing: 8

                                Label {
                                    text: qsTr("State")
                                    color: Theme.textSecondary; font.pixelSize: Theme.fontSizeBase
                                    width: 80; anchors.verticalCenter: parent.verticalCenter
                                }
                                Label {
                                    text: qsTr("Frames")
                                    color: Theme.textSecondary; font.pixelSize: Theme.fontSizeBase
                                    width: 120; anchors.verticalCenter: parent.verticalCenter
                                }
                                Label {
                                    text: qsTr("Node")
                                    color: Theme.textSecondary; font.pixelSize: Theme.fontSizeBase
                                    width: 160; anchors.verticalCenter: parent.verticalCenter
                                }
                                Label {
                                    text: qsTr("Progress")
                                    color: Theme.textSecondary; font.pixelSize: Theme.fontSizeBase
                                    width: 120; anchors.verticalCenter: parent.verticalCenter
                                }
                                Label {
                                    text: qsTr("Retries")
                                    color: Theme.textSecondary; font.pixelSize: Theme.fontSizeBase
                                    width: 60; anchors.verticalCenter: parent.verticalCenter
                                }
                            }
                        }

                        ListView {
                            id: chunksList
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            model: appBridge.chunksModel
                            boundsBehavior: Flickable.StopAtBounds
                            ScrollBar.vertical: ScrollBar {
                                policy: ScrollBar.AsNeeded
                            }

                            delegate: Rectangle {
                                required property var    chunkId
                                required property int    frameStart
                                required property int    frameEnd
                                required property string state
                                required property string assignedNode
                                required property double progress
                                required property int    retryCount
                                required property int    index

                                width: chunksList.width
                                height: 24
                                color: index % 2 === 0 ? Theme.bg : Theme.bgAlt

                                MouseArea {
                                    anchors.fill: parent
                                    acceptedButtons: Qt.RightButton
                                    onClicked: (mouse) => {
                                        chunkMenu.targetChunkId = chunkId
                                        chunkMenu.targetFrameStart = frameStart
                                        chunkMenu.targetFrameEnd   = frameEnd
                                        chunkMenu.popup()
                                    }
                                }

                                Row {
                                    anchors.fill: parent
                                    anchors.leftMargin: 8
                                    anchors.rightMargin: 8
                                    spacing: 8

                                    Label {
                                        text: state
                                        color: stateColor(state)
                                        font.pixelSize: Theme.fontSizeSmall
                                        font.bold: true
                                        width: 80
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                    Label {
                                        text: frameStart === frameEnd
                                              ? "" + frameStart
                                              : frameStart + "-" + frameEnd
                                        color: Theme.textPrimary
                                        font.family: Theme.monoFamily
                                        font.pixelSize: Theme.fontSizeBase
                                        width: 120
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                    Label {
                                        text: assignedNode.length > 0 ? assignedNode : "—"
                                        color: Theme.textPrimary
                                        font.family: Theme.monoFamily
                                        font.pixelSize: Theme.fontSizeBase
                                        width: 160
                                        elide: Text.ElideMiddle
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                    Item {
                                        width: 120
                                        height: parent.height
                                        ProgressBar {
                                            anchors.verticalCenter: parent.verticalCenter
                                            width: parent.width - 6
                                            value: progress
                                            from: 0; to: 1
                                        }
                                    }
                                    Label {
                                        text: retryCount
                                        color: retryCount > 0 ? Theme.warn : Theme.textMuted
                                        font.pixelSize: Theme.fontSizeBase
                                        width: 60
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

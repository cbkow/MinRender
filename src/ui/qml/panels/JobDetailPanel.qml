import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MinRenderUi 1.0   // FrameGrid (C++ QQuickPaintedItem) + Theme singleton

// Two-state panel: Empty (nothing selected) or Detail (a real job is
// selected). Job submission lives in Main.qml's New Job modal, driven
// by appBridge.submissionMode.
Item {
    id: root

    readonly property string mode:
        appBridge.currentJobId.length === 0 ? "empty" : "detail"

    // Chunk-table column widths (px), persisted across launches. Lives
    // at the panel root (not inside detailComponent) so it isn't
    // re-created every time the detail view loads.
    // Persisted height of the grid/preview row in the detail split.
    Settings {
        id: detailLayoutSettings
        category: "jobDetailLayout"

        property real gridRowHeight: 220
    }

    Settings {
        id: chunkColSettings
        category: "chunkTableColumns"

        property int state:    80
        property int frames:   120
        property int node:     160
        property int progress: 120
        property int duration: 70
        property int retries:  60
    }

    Loader {
        anchors.fill: parent
        active: root.mode === "empty"
        sourceComponent: emptyComponent
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
                FlatButton {
                    Layout.alignment: Qt.AlignHCenter
                    iconName: "plus"
                    text: qsTr("New Job")
                    variant: "primary"
                    onClicked: appBridge.submissionMode = true
                }
            }
        }
    }

    Component {
        id: detailComponent
        Rectangle {
            id: detailRoot
            color: Theme.bg

            readonly property var job: appBridge.currentJob

            // Selected chunk row (by chunk id, so the highlight survives
            // model resets). -1 = nothing selected. Left- or right-click
            // selects, so the context menu always fires on the
            // highlighted row.
            property var selectedChunkId: -1

            Connections {
                target: appBridge
                function onCurrentJobIdChanged() { detailRoot.selectedChunkId = -1 }
            }

            // Right-click menu on chunk rows. Lives at the panel level
            // so popup() coordinates make sense and the menu doesn't
            // get clipped by the ListView's clip:true.
            Menu {
                id: chunkMenu
                property var targetChunkId: 0
                property int targetFrameStart: 0
                property int targetFrameEnd: 0
                property string targetState: ""

                MenuItem {
                    text: chunkMenu.targetState === "stopped"
                          ? qsTr("Requeue (resume stopped chunk)")
                          : qsTr("Reassign to another node")
                    onTriggered: appBridge.reassignChunk(chunkMenu.targetChunkId, "")
                }
                MenuItem {
                    // Terminal 'stopped': render killed, never
                    // re-dispatched, frames leave the grid/progress;
                    // the job finishes "partial" around it.
                    text: appBridge.leaderSupportsChunkStop
                          ? qsTr("Stop chunk")
                          : qsTr("Stop chunk (needs leader upgrade)")
                    enabled: appBridge.leaderSupportsChunkStop
                             && (chunkMenu.targetState === "pending"
                                 || chunkMenu.targetState === "assigned")
                    onTriggered: appBridge.stopChunk(chunkMenu.targetChunkId)
                }
                MenuSeparator {}
                MenuItem {
                    // Opens the submission form seeded from this job's
                    // manifest, frame range preset to the chunk — tweak
                    // (e.g. chunk size 1) and submit as a new job.
                    text: qsTr("Submit as separate job…")
                    onTriggered: appBridge.openChunkResubmitEditor(
                        appBridge.currentJobId,
                        chunkMenu.targetFrameStart,
                        chunkMenu.targetFrameEnd)
                }
            }

            function stateColor(s) {
                switch (s) {
                case "active":    return Theme.success
                case "paused":    return Theme.warn
                case "cancelled": return Theme.error
                case "completed": return Theme.info
                case "stopped":   return Theme.textMuted
                default:          return Theme.textSecondary
                }
            }

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                // --- Header (full width, fixed height) ---
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
                            StatusBadge {
                                text: job.state || "—"
                                fillColor: stateColor(job.state || "")
                            }
                        }

                        // Progress line
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Progress {
                                Layout.fillWidth: true
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
                            // Priority is editable post-submission: dispatch
                            // re-sorts on every chunk request, so a change
                            // takes effect on the next dispatch. Fine-grained
                            // order (including across priority groups, which
                            // adopts the drop target's priority) is set by
                            // the drag handles in the jobs panel.
                            Label {
                                text: qsTr("priority")
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fontSizeSmall
                            }
                            SpinBox {
                                id: prioritySpin
                                from: 1
                                to: 999
                                editable: true
                                // Plain `value:` bindings break on first user
                                // edit; this keeps tracking the backend value
                                // whenever the user isn't interacting.
                                Binding on value {
                                    value: job.priority || 50
                                    when: !prioritySpin.activeFocus
                                    restoreMode: Binding.RestoreBindingOrValue
                                }
                                onValueModified: appBridge.setJobPriority(
                                    appBridge.currentJobId, value)
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
                            spacing: 0

                            FlatButton {
                                visible: job.state === "active"
                                iconName: "pause"
                                text: qsTr("Pause")
                                onClicked: appBridge.pauseJob(appBridge.currentJobId)
                            }
                            FlatButton {
                                visible: job.state === "paused"
                                iconName: "play"
                                text: qsTr("Resume")
                                variant: "primary"
                                onClicked: appBridge.resumeJob(appBridge.currentJobId)
                            }
                            FlatButton {
                                iconName: "arrow-clockwise"
                                text: qsTr("Retry failed")
                                enabled: (job.failedChunks || 0) > 0
                                onClicked: appBridge.retryFailedChunks(appBridge.currentJobId)
                            }
                            FlatButton {
                                iconName: "arrows-counter-clockwise"
                                text: qsTr("Requeue")
                                onClicked: appBridge.requeueJob(appBridge.currentJobId)
                            }
                            FlatButton {
                                iconName: "folder-open"
                                text: qsTr("Open output")
                                ToolTip.text: qsTr("Reveal the job's render folder in the file manager. Uses Path Mappings to translate canonical paths.")
                                ToolTip.visible: hovered
                                ToolTip.delay: 600
                                onClicked: appBridge.openJobOutput(appBridge.currentJobId)
                            }
                            Item { Layout.fillWidth: true }
                            FlatButton {
                                iconName: "x"
                                text: qsTr("Cancel")
                                onClicked: appBridge.cancelJob(appBridge.currentJobId)
                            }
                            FlatButton {
                                iconName: "trash"
                                text: qsTr("Delete")
                                variant: "danger"
                                onClicked: appBridge.deleteJob(appBridge.currentJobId)
                            }
                        }
                    }
                }

                // --- Content area (frame grid + preview | chunk list) ---
                SplitView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    orientation: Qt.Vertical

                    // Frame grid — full detail width (the latest-frame
                    // preview lives under the node list). Height persists
                    // via detailLayoutSettings; the drag handle below
                    // trades grid room against the chunk table.
                    Rectangle {
                        color: Theme.frameBg
                        SplitView.preferredHeight: detailLayoutSettings.gridRowHeight
                        SplitView.minimumHeight: 100
                        onHeightChanged: {
                            if (SplitView.view)
                                detailLayoutSettings.gridRowHeight = height
                        }

                        Flickable {
                            id: gridFlick
                            anchors.fill: parent
                            anchors.margins: 4
                            clip: true
                            contentWidth: width
                            contentHeight: Math.max(frameGrid.implicitHeight, height)
                            boundsBehavior: Flickable.StopAtBounds
                            ScrollBar.vertical: MrScrollBar {}

                            // Fixed 10px cells (stable hit targets for
                            // upcoming per-frame selection); large ranges
                            // scroll inside this Flickable.
                            FrameGrid {
                                id: frameGrid
                                width: gridFlick.width
                                height: Math.max(implicitHeight, gridFlick.height)
                                model: appBridge.chunksModel
                                frameStart: job.frameStart || 0
                                frameEnd:   job.frameEnd   || 0
                                cellSize:   10
                                bgColor:        Theme.frameBg
                                unclaimedColor: Theme.frameUnclaimed
                                assignedColor:  Theme.frameAssigned
                                renderedColor:  Theme.frameRendered
                                completedColor: Theme.frameCompleted
                                failedColor:    Theme.frameFailed
                                // Outline only single-frame pins; chunk pins
                                // highlight their row in the table instead.
                                selectedFrame:
                                    appBridge.previewPinStart >= 0
                                    && appBridge.previewPinStart === appBridge.previewPinEnd
                                    ? appBridge.previewPinStart : -1

                                MouseArea {
                                    id: gridMouse
                                    anchors.fill: parent
                                    hoverEnabled: true

                                    property int hoverFrame: -1
                                    property var hoverChunk: ({ found: false })

                                    onPositionChanged: (mouse) => {
                                        const f = frameGrid.frameAtPosition(mouse.x, mouse.y)
                                        if (f === hoverFrame)
                                            return
                                        hoverFrame = f
                                        hoverChunk = f >= 0
                                            ? appBridge.chunksModel.chunkForFrame(f)
                                            : { found: false }
                                    }
                                    onExited: { hoverFrame = -1 }

                                    // Click pins the preview to this frame —
                                    // refused (returns false) unless the frame's
                                    // file is already on disk (rendered + copied).
                                    onClicked: (mouse) => {
                                        const f = frameGrid.frameAtPosition(mouse.x, mouse.y)
                                        if (f >= 0)
                                            appBridge.pinPreview(f, f)
                                    }

                                    ToolTip.visible: containsMouse && hoverFrame >= 0
                                    ToolTip.delay: 300
                                    ToolTip.text: {
                                        if (hoverFrame < 0) return ""
                                        let t = qsTr("Frame %1").arg(hoverFrame)
                                        const c = hoverChunk
                                        if (c && c.found === true) {
                                            t += qsTr("\nChunk %1  (%2-%3)")
                                                .arg(c.chunkNumber).arg(c.frameStart).arg(c.frameEnd)
                                            t += "\n" + c.state
                                            if (c.node && c.node.length > 0)
                                                t += qsTr("  ·  %1").arg(c.node)
                                        }
                                        return t
                                    }
                                }
                            }
                        }
                    }

                    // Chunk list — always visible: the minimum height
                    // guarantees the grid row can't squeeze it away.
                    ColumnLayout {
                        SplitView.fillHeight: true
                        SplitView.minimumHeight: 120
                        spacing: 0

                        // Column header row
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 22
                            color: Theme.border
                            Row {
                                anchors.fill: parent
                                anchors.leftMargin: 8
                                anchors.rightMargin: Theme.padding + Theme.scrollBarWidth
                                spacing: 8

                                ResizableHeaderLabel {
                                    text: qsTr("State")
                                    width: chunkColSettings.state
                                    minWidth: 50; defaultWidth: 80
                                    onResizeTo: (w) => chunkColSettings.state = w
                                }
                                ResizableHeaderLabel {
                                    text: qsTr("Frames")
                                    width: chunkColSettings.frames
                                    minWidth: 60; defaultWidth: 120
                                    onResizeTo: (w) => chunkColSettings.frames = w
                                }
                                ResizableHeaderLabel {
                                    text: qsTr("Node")
                                    width: chunkColSettings.node
                                    minWidth: 60; defaultWidth: 160
                                    onResizeTo: (w) => chunkColSettings.node = w
                                }
                                ResizableHeaderLabel {
                                    text: qsTr("Progress")
                                    width: chunkColSettings.progress
                                    minWidth: 60; defaultWidth: 120
                                    onResizeTo: (w) => chunkColSettings.progress = w
                                }
                                ResizableHeaderLabel {
                                    text: qsTr("Duration")
                                    width: chunkColSettings.duration
                                    minWidth: 50; defaultWidth: 70
                                    onResizeTo: (w) => chunkColSettings.duration = w
                                }
                                ResizableHeaderLabel {
                                    text: qsTr("Retries")
                                    width: chunkColSettings.retries
                                    minWidth: 40; defaultWidth: 60
                                    onResizeTo: (w) => chunkColSettings.retries = w
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
                            ScrollBar.vertical: MrScrollBar {}

                            delegate: Rectangle {
                                id: chunkRow

                                required property var    chunkId
                                required property int    frameStart
                                required property int    frameEnd
                                required property string state
                                required property string assignedNode
                                required property string assignedNodeName
                                required property double progress
                                required property double assignedAt
                                required property double completedAt
                                required property int    retryCount
                                required property int    index

                                readonly property bool selected:
                                    detailRoot.selectedChunkId === chunkId

                                width: chunksList.width
                                height: 24
                                color: selected ? Theme.selection
                                     : index % 2 === 0 ? Theme.bg : Theme.bgAlt
                                // Stopped chunks stay listed (this row
                                // is where Requeue lives) but dimmed —
                                // they're out of the job's accounting.
                                opacity: state === "stopped" ? 0.45 : 1.0

                                MouseArea {
                                    anchors.fill: parent
                                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                                    onClicked: (mouse) => {
                                        detailRoot.selectedChunkId = chunkId
                                        if (mouse.button === Qt.RightButton) {
                                            chunkMenu.targetChunkId = chunkId
                                            chunkMenu.targetFrameStart = frameStart
                                            chunkMenu.targetFrameEnd   = frameEnd
                                            chunkMenu.targetState      = state
                                            chunkMenu.popup()
                                            return
                                        }
                                        // Left-click: pin the preview to this
                                        // chunk's newest on-disk frame. Refused
                                        // silently when nothing is copied yet.
                                        appBridge.pinPreview(frameStart, frameEnd)
                                    }
                                }

                                Row {
                                    anchors.fill: parent
                                    anchors.leftMargin: 8
                                    anchors.rightMargin: Theme.padding + Theme.scrollBarWidth
                                    spacing: 8

                                    Label {
                                        text: state
                                        color: stateColor(state)
                                        font.pixelSize: Theme.fontSizeSmall
                                        font.bold: true
                                        width: chunkColSettings.state
                                        elide: Text.ElideRight
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                    Label {
                                        text: frameStart === frameEnd
                                              ? "" + frameStart
                                              : frameStart + "-" + frameEnd
                                        color: Theme.textPrimary
                                        font.family: Theme.monoFamily
                                        font.pixelSize: Theme.fontSizeBase
                                        width: chunkColSettings.frames
                                        elide: Text.ElideRight
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                    Label {
                                        text: assignedNodeName.length > 0 ? assignedNodeName : "—"
                                        color: Theme.textPrimary
                                        font.family: Theme.monoFamily
                                        font.pixelSize: Theme.fontSizeBase
                                        width: chunkColSettings.node
                                        elide: Text.ElideMiddle
                                        anchors.verticalCenter: parent.verticalCenter
                                        // Hostname is the friendly face; the raw node id
                                        // stays reachable for debugging.
                                        ToolTip.text: assignedNode
                                        ToolTip.visible: assignedNode.length > 0 && nodeNameHover.containsMouse
                                        ToolTip.delay: 600
                                        MouseArea {
                                            id: nodeNameHover
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            acceptedButtons: Qt.NoButton
                                        }
                                    }
                                    Item {
                                        width: chunkColSettings.progress
                                        height: parent.height
                                        Progress {
                                            anchors.verticalCenter: parent.verticalCenter
                                            width: parent.width - 6
                                            value: progress
                                        }
                                    }
                                    Label {
                                        // Wall-clock render time; only completed
                                        // chunks carry a completedAt stamp.
                                        text: completedAt > 0 && assignedAt > 0 && completedAt > assignedAt
                                              ? Format.duration(completedAt - assignedAt)
                                              : "—"
                                        color: Theme.textPrimary
                                        font.family: Theme.monoFamily
                                        font.pixelSize: Theme.fontSizeBase
                                        width: chunkColSettings.duration
                                        elide: Text.ElideRight
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                    Label {
                                        text: retryCount
                                        color: retryCount > 0 ? Theme.warn : Theme.textMuted
                                        font.pixelSize: Theme.fontSizeBase
                                        width: chunkColSettings.retries
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

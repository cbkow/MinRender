import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MinRenderUi 1.0

Item {
    id: root

    // Filter chips apply to Monitor Log (local, structured entries).
    // Remote logs are plain text so filter chips don't route through them.
    property bool showInfo: true
    property bool showWarn: true
    property bool showError: true
    property bool autoscroll: true

    readonly property bool taskMode:   appBridge.logSourceId === "__task__"
    readonly property bool peerMode:   appBridge.logSourceId.length > 0 && !taskMode
    readonly property bool remoteMode: peerMode || taskMode   // "anything but local"

    function levelColor(level) {
        switch (level) {
        case "INFO":  return Theme.info
        case "WARN":  return Theme.warn
        case "ERROR": return Theme.error
        default:      return Theme.textSecondary
        }
    }

    function levelVisible(level) {
        switch (level) {
        case "INFO":  return root.showInfo
        case "WARN":  return root.showWarn
        case "ERROR": return root.showError
        default:      return true
        }
    }

    function formatTs(ms) {
        return Qt.formatDateTime(new Date(ms), "HH:mm:ss.zzz")
    }

    // Match old ImGui keyword-based coloring for remote log lines: we
    // can't parse level out of a plain text line reliably, so match on
    // "[WARN]" / "[ERROR]" substrings.
    function lineColor(line) {
        if (line.indexOf("[ERROR]") !== -1) return Theme.error
        if (line.indexOf("[WARN]")  !== -1) return Theme.warn
        return Theme.textSecondary
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // --- Toolbar ---
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 32
            color: Theme.surface

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 6
                anchors.rightMargin: 6
                spacing: 4

                // Source picker — "Monitor Log" + each alive peer. Model
                // comes from AppBridge so peer list stays in sync with
                // the discovery pass.
                ComboBox {
                    id: sourcePicker
                    Layout.preferredWidth: 220
                    model: appBridge.logSources
                    textRole: "label"
                    valueRole: "id"
                    onActivated: appBridge.logSourceId = currentValue
                    // Keep UI in sync if logSourceId changes from elsewhere
                    // (not currently; future-proofing).
                    Component.onCompleted: {
                        for (let i = 0; i < count; ++i) {
                            if (model[i].id === appBridge.logSourceId) {
                                currentIndex = i
                                return
                            }
                        }
                        currentIndex = 0
                    }
                }

                ToolSeparator { Layout.fillHeight: true }

                // Level filter chips — only meaningful for Monitor Log.
                ToolButton {
                    text: qsTr("Info")
                    enabled: !root.remoteMode
                    checkable: true
                    checked: root.showInfo
                    onToggled: root.showInfo = checked
                }
                ToolButton {
                    text: qsTr("Warn")
                    enabled: !root.remoteMode
                    checkable: true
                    checked: root.showWarn
                    onToggled: root.showWarn = checked
                }
                ToolButton {
                    text: qsTr("Error")
                    enabled: !root.remoteMode
                    checkable: true
                    checked: root.showError
                    onToggled: root.showError = checked
                }

                ToolSeparator { Layout.fillHeight: true }

                CheckBox {
                    text: qsTr("Autoscroll")
                    checked: root.autoscroll
                    onToggled: root.autoscroll = checked
                }

                Item { Layout.fillWidth: true }

                Label {
                    text: root.taskMode
                        ? qsTr("%1 chunks").arg(taskChunkList.count)
                        : (root.peerMode
                           ? qsTr("%1 lines").arg(remoteList.count)
                           : qsTr("%1 entries").arg(logList.count))
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSizeSmall
                }

                ToolSeparator { Layout.fillHeight: true }

                Button {
                    text: qsTr("Clear")
                    flat: true
                    enabled: !root.remoteMode
                    onClicked: appBridge.logModel.clear()
                }
            }
        }

        // --- Monitor Log (local, structured) ---
        ListView {
            id: logList
            visible: !root.remoteMode
            Layout.fillWidth: true
            Layout.fillHeight: !root.remoteMode
            clip: true
            model: appBridge.logModel
            boundsBehavior: Flickable.StopAtBounds
            reuseItems: true
            cacheBuffer: 400

            property bool atBottom: true
            onContentYChanged: {
                atBottom = (contentHeight - (contentY + height)) < 40
            }
            onCountChanged: {
                if (root.autoscroll && atBottom)
                    positionViewAtEnd()
            }

            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            delegate: Rectangle {
                required property var    timestamp
                required property string level
                required property string category
                required property string message
                required property int index

                width: logList.width
                visible: root.levelVisible(level)
                height: visible ? Math.max(20, msgText.implicitHeight + 4) : 0

                color: index % 2 === 0 ? Theme.bg : Theme.bgAlt

                Row {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    spacing: 8

                    Text {
                        text: root.formatTs(timestamp)
                        color: Theme.textMuted
                        font.family: Theme.monoFamily
                        font.pixelSize: Theme.fontSizeSmall
                        width: 84
                    }
                    Text {
                        text: level
                        color: root.levelColor(level)
                        font.family: Theme.monoFamily
                        font.pixelSize: Theme.fontSizeSmall
                        font.bold: true
                        width: 48
                    }
                    Text {
                        text: category
                        color: Theme.textSecondary
                        font.family: Theme.monoFamily
                        font.pixelSize: Theme.fontSizeSmall
                        width: 120
                        elide: Text.ElideRight
                    }
                    Text {
                        id: msgText
                        text: message
                        color: Theme.textPrimary
                        font.family: Theme.monoFamily
                        font.pixelSize: Theme.fontSizeSmall
                        width: parent.width - 276
                        wrapMode: Text.Wrap
                    }
                }
            }
        }

        // --- Remote node log (plain text, refreshed every 3 s) ---
        ListView {
            id: remoteList
            visible: root.peerMode
            Layout.fillWidth: true
            Layout.fillHeight: root.peerMode
            clip: true
            model: appBridge.remoteLogLines
            boundsBehavior: Flickable.StopAtBounds
            reuseItems: true

            property bool atBottom: true
            onContentYChanged: {
                atBottom = (contentHeight - (contentY + height)) < 40
            }
            onCountChanged: {
                if (root.autoscroll && atBottom)
                    positionViewAtEnd()
            }

            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            delegate: Rectangle {
                required property string modelData
                required property int    index

                width: remoteList.width
                height: Math.max(18, lineText.implicitHeight + 2)
                color: index % 2 === 0 ? Theme.bg : Theme.bgAlt

                Text {
                    id: lineText
                    anchors {
                        left: parent.left
                        right: parent.right
                        verticalCenter: parent.verticalCenter
                        leftMargin: 8
                        rightMargin: 8
                    }
                    text: modelData
                    color: root.lineColor(modelData)
                    font.family: Theme.monoFamily
                    font.pixelSize: Theme.fontSizeSmall
                    wrapMode: Text.Wrap
                }
            }

            // Empty-state hint
            Label {
                anchors.centerIn: parent
                visible: remoteList.count === 0
                text: appBridge.farmRunning
                    ? qsTr("No log data for this node yet")
                    : qsTr("Farm not running")
                color: Theme.textMuted
                font.pixelSize: Theme.fontSizeBase
            }
        }

        // --- Task Output (per-chunk stdout of the selected job) ---
        // Two panes: left = chunk list for the selected job, right =
        // file contents for the selected chunk. Both refresh every 3 s
        // while the mode is active.
        Item {
            visible: root.taskMode
            Layout.fillWidth: true
            Layout.fillHeight: root.taskMode

            // No job selected → empty state.
            Label {
                anchors.centerIn: parent
                visible: appBridge.currentJobId.length === 0
                text: qsTr("Select a job in the Jobs list to view its task output.")
                color: Theme.textMuted
                font.pixelSize: Theme.fontSizeBase
                wrapMode: Text.WordWrap
                width: parent.width - 40
                horizontalAlignment: Text.AlignHCenter
            }

            SplitView {
                anchors.fill: parent
                visible: appBridge.currentJobId.length > 0
                orientation: Qt.Horizontal

                // Left pane: chunk list.
                ListView {
                    id: taskChunkList
                    SplitView.preferredWidth: 220
                    SplitView.minimumWidth: 140
                    clip: true
                    model: appBridge.taskOutputChunks
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                    currentIndex: appBridge.selectedTaskChunkIndex

                    delegate: Rectangle {
                        required property var    modelData
                        required property int    index

                        width: taskChunkList.width
                        height: 24
                        color: index === appBridge.selectedTaskChunkIndex
                               ? Theme.selection
                               : (index % 2 === 0 ? Theme.bg : Theme.bgAlt)

                        Label {
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            verticalAlignment: Text.AlignVCenter
                            text: modelData.displayLabel
                            color: Theme.textPrimary
                            font.family: Theme.monoFamily
                            font.pixelSize: Theme.fontSizeSmall
                            elide: Text.ElideRight
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: appBridge.selectedTaskChunkIndex = index
                        }

                        ToolTip {
                            visible: chunkHover.containsMouse
                            delay: 400
                            text: modelData.nodeId + " — " + modelData.rangeStr
                        }
                        HoverHandler { id: chunkHover }
                    }

                    Label {
                        anchors.centerIn: parent
                        visible: taskChunkList.count === 0
                        text: qsTr("No chunk output yet")
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontSizeSmall
                    }
                }

                // Right pane: selected chunk contents.
                ListView {
                    id: taskContentList
                    SplitView.fillWidth: true
                    clip: true
                    model: appBridge.taskOutputLines
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                    property bool atBottom: true
                    onContentYChanged: {
                        atBottom = (contentHeight - (contentY + height)) < 40
                    }
                    onCountChanged: {
                        if (root.autoscroll && atBottom)
                            positionViewAtEnd()
                    }

                    delegate: Rectangle {
                        required property string modelData
                        required property int    index

                        width: taskContentList.width
                        height: Math.max(18, contentText.implicitHeight + 2)
                        color: index % 2 === 0 ? Theme.bg : Theme.bgAlt

                        Text {
                            id: contentText
                            anchors {
                                left: parent.left
                                right: parent.right
                                verticalCenter: parent.verticalCenter
                                leftMargin: 8
                                rightMargin: 8
                            }
                            text: modelData
                            color: root.lineColor(modelData)
                            font.family: Theme.monoFamily
                            font.pixelSize: Theme.fontSizeSmall
                            wrapMode: Text.Wrap
                        }
                    }

                    Label {
                        anchors.centerIn: parent
                        visible: taskContentList.count === 0
                        text: appBridge.selectedTaskChunkIndex < 0
                            ? qsTr("Select a chunk on the left")
                            : qsTr("(Empty)")
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontSizeSmall
                    }
                }
            }
        }
    }
}

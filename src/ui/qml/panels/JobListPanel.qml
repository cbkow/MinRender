import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    // Multi-select state. Key = jobId, value = true. A plain JS object
    // so we can reassign root.checked = {...} to force QML reactivity.
    property var checked: ({})

    function checkedCount() {
        let n = 0
        for (const k in checked) if (checked[k]) n += 1
        return n
    }

    function isChecked(jobId) {
        return checked[jobId] === true
    }

    function setChecked(jobId, on) {
        const next = Object.assign({}, checked)
        if (on) next[jobId] = true
        else delete next[jobId]
        checked = next
    }

    function clearChecked() {
        checked = ({})
    }

    function stateColor(state) {
        switch (state) {
        case "active":    return "#9ece6a"
        case "paused":    return "#e0af68"
        case "cancelled": return "#f7768e"
        case "completed": return "#7aa2f7"
        default:          return "#888"
        }
    }

    function formatTs(ms) {
        if (ms <= 0) return qsTr("—")
        return Qt.formatDateTime(new Date(ms), "yyyy-MM-dd HH:mm")
    }

    // Column widths (px). Checkbox | Name | State | Progress | Created
    readonly property int colCheck:    28
    readonly property int colState:    90
    readonly property int colProgress: 140
    readonly property int colCreated:  140

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // --- Toolbar ---
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 32
            color: "#1a1a1a"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 6
                anchors.rightMargin: 6
                spacing: 6

                Label {
                    text: qsTr("Jobs")
                    color: "#cccccc"
                    font.bold: true
                    font.pixelSize: 12
                }

                Label {
                    text: qsTr("· %1").arg(jobList.count)
                    color: "#666"
                    font.pixelSize: 11
                }

                Label {
                    visible: root.checkedCount() > 0
                    text: qsTr("· %1 selected").arg(root.checkedCount())
                    color: "#7aa2f7"
                    font.pixelSize: 11
                }

                Item { Layout.fillWidth: true }

                Button {
                    text: qsTr("New Job…")
                    onClicked: appBridge.requestSubmissionMode()
                }
            }
        }

        // --- Column headers ---
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 22
            color: "#222222"

            Row {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                spacing: 8

                CheckBox {
                    id: selectAll
                    width: root.colCheck
                    anchors.verticalCenter: parent.verticalCenter
                    tristate: true
                    checkState: {
                        const total = jobList.count
                        const n = root.checkedCount()
                        if (n === 0) return Qt.Unchecked
                        if (n === total) return Qt.Checked
                        return Qt.PartiallyChecked
                    }
                    // Suppress tristate cycling: two-state behaviour —
                    // select-all / clear-all.
                    nextCheckState: function() {
                        if (checkState === Qt.Checked) return Qt.Unchecked
                        return Qt.Checked
                    }
                    onToggled: {
                        if (checkState === Qt.Checked) {
                            const next = {}
                            for (let i = 0; i < jobList.count; ++i) {
                                const id = appBridge.jobsModel.data(
                                    appBridge.jobsModel.index(i, 0),
                                    Qt.UserRole + 1)   // JobIdRole
                                next[id] = true
                            }
                            root.checked = next
                        } else {
                            root.clearChecked()
                        }
                    }
                }

                Label {
                    text: qsTr("Name")
                    color: "#888"
                    font.pixelSize: 11
                    width: parent.width - root.colCheck - root.colState
                           - root.colProgress - root.colCreated - 4 * 8
                    anchors.verticalCenter: parent.verticalCenter
                    elide: Text.ElideRight
                }
                Label {
                    text: qsTr("State")
                    color: "#888"
                    font.pixelSize: 11
                    width: root.colState
                    anchors.verticalCenter: parent.verticalCenter
                }
                Label {
                    text: qsTr("Progress")
                    color: "#888"
                    font.pixelSize: 11
                    width: root.colProgress
                    anchors.verticalCenter: parent.verticalCenter
                }
                Label {
                    text: qsTr("Created")
                    color: "#888"
                    font.pixelSize: 11
                    width: root.colCreated
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
        }

        // --- Job rows ---
        ListView {
            id: jobList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: appBridge.jobsModel
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            delegate: Rectangle {
                required property string jobId
                required property string name
                required property string state
                required property double progress
                required property int    totalChunks
                required property int    doneChunks
                required property int    failedChunks
                required property qint64 createdAt
                required property int    index

                width: jobList.width
                height: 32
                color: {
                    if (appBridge.currentJobId === jobId) return "#2a3b5c"
                    return index % 2 === 0 ? "#181818" : "#1c1c1c"
                }

                Row {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    spacing: 8

                    CheckBox {
                        width: root.colCheck
                        anchors.verticalCenter: parent.verticalCenter
                        checked: root.isChecked(jobId)
                        onToggled: root.setChecked(jobId, checked)
                    }

                    Label {
                        text: name
                        color: "#cccccc"
                        font.pixelSize: 12
                        font.family: "monospace"
                        width: parent.width - root.colCheck - root.colState
                               - root.colProgress - root.colCreated - 4 * 8
                        anchors.verticalCenter: parent.verticalCenter
                        elide: Text.ElideMiddle
                    }

                    Label {
                        text: state
                        color: root.stateColor(state)
                        font.pixelSize: 11
                        font.bold: true
                        width: root.colState
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    Item {
                        width: root.colProgress
                        height: parent.height
                        ProgressBar {
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - 40
                            value: progress
                            from: 0; to: 1
                        }
                        Label {
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("%1/%2").arg(doneChunks).arg(totalChunks)
                            color: "#888"
                            font.pixelSize: 10
                        }
                    }

                    Label {
                        text: root.formatTs(createdAt)
                        color: "#888"
                        font.pixelSize: 11
                        width: root.colCreated
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    // Accept clicks but don't block the CheckBox's own
                    // MouseArea which sits above.
                    propagateComposedEvents: true
                    onClicked: (mouse) => {
                        if (mouse.button === Qt.RightButton) {
                            jobMenu.targetId = jobId
                            jobMenu.targetState = state
                            jobMenu.popup()
                            mouse.accepted = true
                        } else {
                            appBridge.currentJobId = jobId
                            mouse.accepted = false  // let CheckBox see it too
                        }
                    }
                }
            }
        }
    }

    Menu {
        id: jobMenu
        property string targetId: ""
        property string targetState: ""

        MenuItem {
            text: qsTr("Pause")
            enabled: jobMenu.targetState === "active"
            onTriggered: appBridge.pauseJob(jobMenu.targetId)
        }
        MenuItem {
            text: qsTr("Resume")
            enabled: jobMenu.targetState === "paused"
            onTriggered: appBridge.resumeJob(jobMenu.targetId)
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Retry failed chunks")
            onTriggered: appBridge.retryFailedChunks(jobMenu.targetId)
        }
        MenuItem {
            text: qsTr("Requeue")
            onTriggered: appBridge.requeueJob(jobMenu.targetId)
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Cancel")
            onTriggered: appBridge.cancelJob(jobMenu.targetId)
        }
        MenuItem {
            text: qsTr("Archive")
            onTriggered: appBridge.archiveJob(jobMenu.targetId)
        }
        MenuItem {
            text: qsTr("Delete")
            onTriggered: appBridge.deleteJob(jobMenu.targetId)
        }
    }
}

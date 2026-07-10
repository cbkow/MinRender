import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MinRenderUi 1.0

// Six-section cleanup browser. Opens with an empty state; Rescan runs
// AppBridge.scanFarmCleanup synchronously (~instant for small farms,
// may take a second or two on a slow share) and populates each section
// with checkboxable rows. Per-section action buttons fire the matching
// cleanup action on only the checked ids.
MrDialog {
    id: root
    title: qsTr("Farm Cleanup")
    width: 720
    height: Math.min(760, parent ? parent.height - 80 : 700)

    // Six groups returned by AppBridge.scanFarmCleanup.
    property var finishedJobs:       []
    property var archivedJobs:       []
    property var orphanedDirs:       []
    property var stalePeers:         []
    property var staleStagingDirs:   []
    property var failedStagingCopies:[]
    property bool scanIsLeader: false
    property bool didScan: false

    // Selection state. Each section's row checkbox flips root.checked[id].
    property var checked: ({})

    function isChecked(id)    { return checked[id] === true }
    function setChecked(id, v) {
        const next = Object.assign({}, checked)
        if (v) next[id] = true
        else   delete next[id]
        checked = next
    }
    function setAllChecked(list, v) {
        const next = Object.assign({}, checked)
        for (let i = 0; i < list.length; ++i) {
            if (v) next[list[i].id] = true
            else   delete next[list[i].id]
        }
        checked = next
    }
    function selectedInList(list) {
        const out = []
        for (let i = 0; i < list.length; ++i)
            if (checked[list[i].id]) out.push(list[i].id)
        return out
    }

    function rescan() {
        appBridge.requestFarmCleanupScan()
    }

    function applyAction(action, list) {
        const ids = selectedInList(list)
        if (ids.length === 0) return
        // Fire-and-forget: the action's follow-up rescan repopulates
        // via onFarmCleanupScanReady; cleanupBusy gates the buttons.
        appBridge.requestFarmCleanupAction(action, ids)
    }

    Connections {
        target: appBridge
        function onFarmCleanupScanReady(r) {
            if (!root.visible) return
            root.finishedJobs        = r.finished_jobs         || []
            root.archivedJobs        = r.archived_jobs         || []
            root.orphanedDirs        = r.orphaned_dirs         || []
            root.stalePeers          = r.stale_peers           || []
            root.staleStagingDirs    = r.stale_staging_dirs    || []
            root.failedStagingCopies = r.failed_staging_copies || []
            root.scanIsLeader        = r.is_leader === true
            root.checked             = ({})
            root.didScan             = true
        }
    }

    // Auto-scan on open — async, so the dialog paints immediately and
    // the spinner shows while the share walk runs.
    onOpened: rescan()
    onClosed: {
        didScan = false
        checked = ({})
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            Label {
                text: root.scanIsLeader
                    ? qsTr("You are the leader — DB-backed sections are populated.")
                    : qsTr("You are a worker — finished/archived jobs need the leader to scan.")
                color: Theme.textMuted
                font.pixelSize: Theme.fontSizeSmall
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            BusyIndicator {
                visible: appBridge.cleanupBusy
                running: visible
                implicitWidth: 20
                implicitHeight: 20
            }
            FlatButton {
                iconName: "arrows-clockwise"
                text: qsTr("Rescan")
                enabled: !appBridge.cleanupBusy
                onClicked: root.rescan()
            }
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
                spacing: 12

                CleanupSection {
                    title: qsTr("Finished jobs (completed / cancelled)")
                    items: root.finishedJobs
                    actionLabel: qsTr("Archive selected")
                    emptyHint: root.scanIsLeader
                        ? qsTr("No finished jobs.")
                        : qsTr("Worker can't list finished jobs — only the leader has DB access.")
                    onActionTriggered: root.applyAction("archive", root.finishedJobs)
                }

                CleanupSection {
                    title: qsTr("Archived jobs")
                    items: root.archivedJobs
                    actionLabel: qsTr("Delete selected")
                    emptyHint: root.scanIsLeader
                        ? qsTr("No archived jobs.")
                        : qsTr("Worker can't list archived jobs.")
                    onActionTriggered: root.applyAction("delete_jobs", root.archivedJobs)
                }

                CleanupSection {
                    title: qsTr("Orphaned job directories (no matching record)")
                    items: root.orphanedDirs
                    actionLabel: qsTr("Delete selected")
                    emptyHint: qsTr("No orphaned job directories.")
                    onActionTriggered: root.applyAction("delete_dirs", root.orphanedDirs)
                }

                CleanupSection {
                    title: qsTr("Offline peers")
                    items: root.stalePeers
                    actionLabel: qsTr("Remove selected")
                    emptyHint: qsTr("No offline peers.")
                    onActionTriggered: root.applyAction("remove_peers", root.stalePeers)
                }

                CleanupSection {
                    title: qsTr("Empty staging directories")
                    items: root.staleStagingDirs
                    actionLabel: qsTr("Delete selected")
                    emptyHint: qsTr("No empty staging directories.")
                    onActionTriggered: root.applyAction("delete_dirs", root.staleStagingDirs)
                }

                CleanupSection {
                    title: qsTr("Non-empty staging copies (failed or in-progress)")
                    items: root.failedStagingCopies
                    actionLabel: qsTr("Delete selected")
                    emptyHint: qsTr("No lingering staging copies.")
                    onActionTriggered: root.applyAction("delete_dirs", root.failedStagingCopies)
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            FlatButton {
                text: qsTr("Close")
                onClicked: root.close()
            }
        }
    }

    // --- Section component ---
    component CleanupSection : Rectangle {
        id: section
        property string title: ""
        property var    items: []
        property string actionLabel: qsTr("Apply")
        property string emptyHint: qsTr("Nothing to show.")
        signal actionTriggered

        Layout.fillWidth: true
        implicitHeight: sectionColumn.implicitHeight + 16
        color: Theme.surface
        radius: Theme.radiusBase

        ColumnLayout {
            id: sectionColumn
            anchors {
                left: parent.left
                right: parent.right
                top: parent.top
                margins: 8
            }
            spacing: 4

            RowLayout {
                Layout.fillWidth: true
                // Tri-state select-all: wipe a whole category in two
                // clicks (select all → action) without ticking rows.
                CheckBox {
                    visible: section.items.length > 0
                    tristate: true
                    checkState: {
                        const n = root.selectedInList(section.items).length
                        return n === 0 ? Qt.Unchecked
                             : n === section.items.length ? Qt.Checked
                             : Qt.PartiallyChecked
                    }
                    // nextCheckState drives the click cycle: anything
                    // not-fully-checked selects all, checked clears.
                    nextCheckState: function() {
                        return checkState === Qt.Checked ? Qt.Unchecked : Qt.Checked
                    }
                    onClicked: root.setAllChecked(
                        section.items, checkState === Qt.Checked)
                    ToolTip.text: qsTr("Select all in this section")
                    ToolTip.visible: hovered
                    ToolTip.delay: 500
                }
                SectionHeader {
                    text: section.title + " · " + section.items.length
                }
                Item { Layout.fillWidth: true }
                FlatButton {
                    text: section.actionLabel
                    enabled: section.items.length > 0
                             && root.selectedInList(section.items).length > 0
                             && !appBridge.cleanupBusy
                    onClicked: section.actionTriggered()
                }
            }

            Label {
                visible: section.items.length === 0 && root.didScan
                text: section.emptyHint
                color: Theme.textMuted
                font.pixelSize: Theme.fontSizeSmall
            }

            Repeater {
                model: section.items
                delegate: RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    required property var modelData

                    CheckBox {
                        checked: root.isChecked(modelData.id)
                        onToggled: root.setChecked(modelData.id, checked)
                    }
                    Label {
                        text: modelData.label
                        color: Theme.textPrimary
                        font.family: Theme.monoFamily
                        font.pixelSize: Theme.fontSizeBase
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                    }
                    Label {
                        text: modelData.detail
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontSizeSmall
                    }
                }
            }
        }
    }
}

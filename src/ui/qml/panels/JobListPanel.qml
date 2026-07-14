import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MinRenderUi 1.0

Item {
    id: root

    // Fired by the New Job button. Main.qml hooks this to open the
    // submission Dialog. (We don't reach into Main.qml's id directly to
    // keep the panel reusable.)
    signal newJobRequested

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

    function checkedJobIds() {
        const out = []
        for (const k in checked)
            if (checked[k]) out.push(k)
        return out
    }

    // Apply a single-job AppBridge action across the checked set, then
    // clear selection. Used by the bulk-action strip.
    function applyToChecked(actionFn) {
        const ids = checkedJobIds()
        for (let i = 0; i < ids.length; ++i)
            actionFn(ids[i])
        clearChecked()
    }

    // Standard desktop-list selection model — driven entirely by row
    // click + modifier keys, no checkboxes:
    //   click           → select only this row
    //   cmd / ctrl-click → toggle this row in the selection
    //   shift-click     → range-select from currentJobId to this row
    // currentJobId is the "primary" (drives JobDetailPanel) and is
    // always one of the checked rows when any are checked.
    function jobIdAt(rowIdx) {
        return appBridge.jobsModel.jobIdAt(rowIdx)
    }

    function selectRange(fromJobId, toJobId) {
        let fromIdx = -1, toIdx = -1
        for (let i = 0; i < jobList.count; ++i) {
            const id = jobIdAt(i)
            if (id === fromJobId) fromIdx = i
            if (id === toJobId)   toIdx = i
        }
        if (fromIdx < 0 || toIdx < 0) {
            // Anchor missing — fall back to single select.
            const fallback = {}
            fallback[toJobId] = true
            checked = fallback
            return
        }
        const lo = Math.min(fromIdx, toIdx)
        const hi = Math.max(fromIdx, toIdx)
        const next = {}
        for (let i = lo; i <= hi; ++i)
            next[jobIdAt(i)] = true
        checked = next
    }

    function handleRowClick(jobId, mouse) {
        const isShift = (mouse.modifiers & Qt.ShiftModifier) !== 0
        // Cmd on macOS reports as MetaModifier; Ctrl on Windows/Linux
        // reports as ControlModifier. Both are treated as "additive
        // toggle" — same UX expectation.
        const isToggle = (mouse.modifiers
            & (Qt.ControlModifier | Qt.MetaModifier)) !== 0

        if (isShift) {
            const anchor = appBridge.currentJobId || jobId
            selectRange(anchor, jobId)
            appBridge.currentJobId = jobId
            return
        }

        if (isToggle) {
            const next = Object.assign({}, checked)
            if (next[jobId]) {
                delete next[jobId]
                checked = next
                // If we cleared the primary, slide to any remaining
                // selection so JobDetailPanel still has something to show.
                if (appBridge.currentJobId === jobId) {
                    const remaining = Object.keys(next)
                    appBridge.currentJobId = remaining.length > 0 ? remaining[0] : ""
                }
            } else {
                next[jobId] = true
                checked = next
                appBridge.currentJobId = jobId
            }
            return
        }

        // Plain click — collapse selection to this row.
        const next = {}
        next[jobId] = true
        checked = next
        appBridge.currentJobId = jobId
    }

    function stateColor(state) {
        switch (state) {
        case "active":    return Theme.success
        case "paused":    return Theme.warn
        case "cancelled": return Theme.error
        case "completed": return Theme.info
        case "partial":   return Theme.warn
        default:          return Theme.textSecondary
        }
    }

    // Display-only refinement: a job that completed around stopped
    // chunks reads "partial" — the wire state stays "completed" so old
    // clients are untouched.
    function displayState(state, stoppedChunks) {
        return state === "completed" && stoppedChunks > 0
               ? "partial" : state
    }

    function formatTs(ms) {
        if (ms <= 0) return qsTr("—")
        return Qt.formatDateTime(new Date(ms), "yyyy-MM-dd HH:mm")
    }

    // 1 s heartbeat so active jobs' Duration cells tick live. Model
    // dataChanged only fires when a job's fields move, which for a
    // long chunk can be minutes — the timer keeps elapsed time honest.
    property double nowMs: Date.now()
    Timer {
        interval: 1000
        repeat: true
        running: root.visible
        onTriggered: root.nowMs = Date.now()
    }

    // Wall-clock job duration: first chunk assignment → last chunk
    // completion. Active jobs tick against now; paused/cancelled/failed
    // freeze at their last completion; untouched jobs show "—".
    function jobDuration(state, startMs, endMs) {
        if (startMs <= 0) return "—"
        if (state === "active") return Format.duration(root.nowMs - startMs)
        if (endMs > startMs)    return Format.duration(endMs - startMs)
        return "—"
    }

    // Column widths (px), persisted across launches. Every column is
    // fixed-width and left-aligned — no fill-remainder column, so a
    // resize never shifts the columns to its left and the grip stays
    // under the cursor. Leftover width is empty row background.
    // Handle | Pri | Name | State | Progress | Duration | Created
    Settings {
        id: colSettings
        category: "jobsTableColumns"

        property int pri:      36
        property int name:     300
        property int state:    90
        property int progress: 280
        property int duration: 80
        property int created:  140
    }
    readonly property int colHandle: 22
    property alias colPri:      colSettings.pri
    property alias colName:     colSettings.name
    property alias colState:    colSettings.state
    property alias colProgress: colSettings.progress
    property alias colDuration: colSettings.duration
    property alias colCreated:  colSettings.created

    // --- Drag-to-reorder state ---
    // Any row can be dragged anywhere: the job is spliced next to the row
    // at the drop line, and if that row is in a different priority group
    // the moved job adopts its priority (leader-side, DatabaseManager::
    // moveJob) — the visible list order IS the dispatch order. At a group
    // boundary the drop line means "before the row below it", so the job
    // adopts that lower row's priority; a drop below the last row adopts
    // the last row's. Model refreshes are paused for the duration
    // (appBridge.jobsRefreshPaused), so indices are stable.
    property string dragJobId: ""
    property int dragIndex: -1
    property int dropGap: -1        // insertion gap (row index the job lands before); -1 = invalid

    function beginDrag(jobId, rowIdx) {
        appBridge.jobsRefreshPaused = true
        dragJobId = jobId
        dragIndex = rowIdx
        dropGap = -1
        // Grabbing a row makes it the thing being interacted with: select
        // it (exclusively) right away, so the highlight and detail panel
        // are on it during the drag and travel with it after the drop.
        const next = {}
        next[jobId] = true
        checked = next
        appBridge.currentJobId = jobId
    }

    function updateDrag(contentY) {
        let gap = Math.round(contentY / 32)   // 32 = fixed row height
        if (gap < 0 || gap > jobList.count) {
            dropGap = -1                       // outside the list
            return
        }
        // Dropping into its own slot is a no-op — hide the indicator.
        if (gap === dragIndex || gap === dragIndex + 1) {
            dropGap = -1
            return
        }
        dropGap = gap
    }

    function endDrag() {
        if (dropGap >= 0) {
            if (dropGap < jobList.count)
                appBridge.moveJob(dragJobId, jobIdAt(dropGap), true)
            else
                appBridge.moveJob(dragJobId, jobIdAt(jobList.count - 1), false)
        }
        dragJobId = ""
        dragIndex = -1
        dropGap = -1
        appBridge.jobsRefreshPaused = false
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // --- Toolbar ---
        PanelHeader {
            Layout.fillWidth: true
            title: qsTr("Jobs")
            subtitle: {
                let s = "· " + jobList.count
                if (root.checkedCount() > 0)
                    s += " · " + root.checkedCount() + " " + qsTr("selected")
                return s
            }

            FlatButton {
                iconName: "plus"
                text: qsTr("New Job")
                variant: "primary"
                onClicked: root.newJobRequested()
            }
        }

        // --- Bulk-action strip (always present) ---
        // Mirrors the right-click jobMenu for single jobs, but loops
        // every action over every checked id and clears the selection
        // afterwards. Always rendered so the available actions stay
        // visible/discoverable; each button is disabled when no rows
        // are checked instead of toggling the whole strip away.
        // Destructive ops still route through bulkConfirmDialog so a
        // stray click can't wipe N jobs.
        Rectangle {
            Layout.fillWidth: true
            color: Theme.toolbar
            implicitHeight: Theme.toolStripHeight

            // 1px bottom divider to seat the strip cleanly under the header.
            Rectangle {
                anchors.left:   parent.left
                anchors.right:  parent.right
                anchors.bottom: parent.bottom
                height: Theme.dividerWidth
                color: Theme.divider
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.padding
                anchors.rightMargin: Theme.spacingTight
                spacing: 0

                Label {
                    text: root.checkedCount() > 0
                        ? qsTr("%1 selected").arg(root.checkedCount())
                        : qsTr("No selection")
                    color: root.checkedCount() > 0
                        ? Theme.textBright
                        : Theme.textMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeBase
                    Layout.rightMargin: Theme.padding
                }
                FlatButton {
                    iconName: "pencil-simple"
                    text: qsTr("Edit")
                    // The edit form is one manifest, one job — single
                    // selection only. Gated on the leader's capability:
                    // old leaders have no /edit route.
                    enabled: root.checkedCount() === 1
                             && appBridge.leaderSupportsJobEdit
                    ToolTip.text: qsTr("The leader is running an older version without job editing")
                    ToolTip.visible: hovered && !appBridge.leaderSupportsJobEdit
                    ToolTip.delay: 500
                    onClicked: appBridge.openJobEditor(root.checkedJobIds()[0])
                }
                FlatButton {
                    iconName: "pause"
                    text: qsTr("Pause")
                    enabled: root.checkedCount() > 0
                    onClicked: root.applyToChecked(
                        function(id) { appBridge.pauseJob(id) })
                }
                FlatButton {
                    iconName: "play"
                    text: qsTr("Resume")
                    enabled: root.checkedCount() > 0
                    onClicked: root.applyToChecked(
                        function(id) { appBridge.resumeJob(id) })
                }
                FlatButton {
                    iconName: "arrow-clockwise"
                    text: qsTr("Retry failed")
                    enabled: root.checkedCount() > 0
                    onClicked: root.applyToChecked(
                        function(id) { appBridge.retryFailedChunks(id) })
                }
                FlatButton {
                    iconName: "arrows-counter-clockwise"
                    text: qsTr("Requeue")
                    enabled: root.checkedCount() > 0
                    onClicked: root.applyToChecked(
                        function(id) { appBridge.requeueJob(id) })
                }
                FlatButton {
                    iconName: "archive"
                    text: qsTr("Archive")
                    enabled: root.checkedCount() > 0
                    onClicked: root.applyToChecked(
                        function(id) { appBridge.archiveJob(id) })
                }
                Item { Layout.fillWidth: true }
                FlatButton {
                    iconName: "x"
                    text: qsTr("Cancel")
                    enabled: root.checkedCount() > 0
                    onClicked: bulkConfirmDialog.openWith(
                        "cancel",
                        qsTr("Cancel %1 job%2?")
                            .arg(root.checkedCount())
                            .arg(root.checkedCount() === 1 ? "" : "s"),
                        qsTr("This stops dispatch and immediately kills in-flight renders on all nodes. The jobs can be requeued later."),
                        function(id) { appBridge.cancelJob(id) })
                }
                FlatButton {
                    iconName: "trash"
                    text: qsTr("Delete")
                    variant: "danger"
                    enabled: root.checkedCount() > 0
                    onClicked: bulkConfirmDialog.openWith(
                        "delete",
                        qsTr("Delete %1 job%2?")
                            .arg(root.checkedCount())
                            .arg(root.checkedCount() === 1 ? "" : "s"),
                        qsTr("Removes job records and SQLite history. Output files on disk are NOT deleted. This cannot be undone."),
                        function(id) { appBridge.deleteJob(id) })
                }
            }
        }

        // --- Column headers ---
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 22
            color: Theme.border

            Row {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: Theme.padding + Theme.scrollBarWidth
                spacing: 8

                Item { width: root.colHandle; height: 1 }

                ResizableHeaderLabel {
                    text: qsTr("Pri")
                    pixelSize: 11
                    width: root.colPri
                    minWidth: 28
                    defaultWidth: 36
                    onResizeTo: (w) => root.colPri = w
                }

                ResizableHeaderLabel {
                    text: qsTr("Name")
                    pixelSize: 11
                    width: root.colName
                    minWidth: 100
                    defaultWidth: 300
                    onResizeTo: (w) => root.colName = w
                }
                ResizableHeaderLabel {
                    text: qsTr("State")
                    pixelSize: 11
                    width: root.colState
                    minWidth: 50
                    defaultWidth: 90
                    onResizeTo: (w) => root.colState = w
                }
                ResizableHeaderLabel {
                    text: qsTr("Progress")
                    pixelSize: 11
                    width: root.colProgress
                    minWidth: 80
                    defaultWidth: 280
                    onResizeTo: (w) => root.colProgress = w
                }
                ResizableHeaderLabel {
                    text: qsTr("Duration")
                    pixelSize: 11
                    width: root.colDuration
                    minWidth: 50
                    defaultWidth: 80
                    onResizeTo: (w) => root.colDuration = w
                }
                ResizableHeaderLabel {
                    text: qsTr("Created")
                    pixelSize: 11
                    width: root.colCreated
                    minWidth: 60
                    defaultWidth: 140
                    onResizeTo: (w) => root.colCreated = w
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
            ScrollBar.vertical: MrScrollBar {}

            delegate: Rectangle {
                required property string jobId
                required property string name
                required property string state
                required property double progress
                required property int    totalChunks
                required property int    doneChunks
                required property int    failedChunks
                required property int    stoppedChunks
                required property var    createdAt
                required property int    priority
                required property double firstAssignedAt
                required property double lastCompletedAt
                required property int    index

                width: jobList.width
                height: 32
                color: root.isChecked(jobId) ? Theme.selection
                     : (index % 2 === 0 ? Theme.bgAlt : Theme.surface)
                // Ghost the row being dragged so the indicator line reads
                // as "this is where it's going".
                opacity: root.dragJobId === jobId ? 0.45 : 1.0

                // Accent left-rule on the "primary" row (currentJobId).
                // Distinguishes the row whose detail is showing from
                // any additionally-checked rows in a multi-select.
                Rectangle {
                    visible: appBridge.currentJobId === jobId
                             && root.checkedCount() > 0
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: 3
                    color: Theme.accent
                }

                Row {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: Theme.padding + Theme.scrollBarWidth
                    spacing: 8

                    Item {
                        width: root.colHandle
                        height: parent.height
                        Icon {
                            anchors.centerIn: parent
                            name: "dots-six-vertical"
                            size: 14
                            color: Theme.textMuted
                        }
                    }

                    Label {
                        text: priority
                        color: Theme.textSecondary
                        font.pixelSize: 11
                        font.family: Theme.monoFamily
                        width: root.colPri
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    Label {
                        text: name
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontSizeBase
                        font.family: Theme.monoFamily
                        width: root.colName
                        anchors.verticalCenter: parent.verticalCenter
                        elide: Text.ElideMiddle
                    }

                    Label {
                        readonly property string shown:
                            root.displayState(state, stoppedChunks)
                        text: shown
                        color: root.stateColor(shown)
                        font.pixelSize: 11
                        font.bold: true
                        width: root.colState
                        elide: Text.ElideRight
                        anchors.verticalCenter: parent.verticalCenter
                        ToolTip.text: qsTr("%1 stopped chunk(s) excluded").arg(stoppedChunks)
                        ToolTip.visible: shown === "partial" && stateHover.hovered
                        ToolTip.delay: 400
                        HoverHandler { id: stateHover }
                    }

                    Item {
                        width: root.colProgress
                        height: parent.height
                        Progress {
                            anchors.verticalCenter: parent.verticalCenter
                            // Reserve room for a "0000/0000" count plus
                            // an 8px gutter so the bar never touches it.
                            width: parent.width - 64
                            value: progress
                        }
                        Label {
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("%1/%2").arg(doneChunks).arg(totalChunks)
                            color: Theme.textSecondary
                            font.pixelSize: 10
                        }
                    }

                    Label {
                        text: root.jobDuration(state, firstAssignedAt, lastCompletedAt)
                        color: Theme.textSecondary
                        font.pixelSize: 11
                        font.family: Theme.monoFamily
                        width: root.colDuration
                        elide: Text.ElideRight
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    Label {
                        text: root.formatTs(createdAt)
                        color: Theme.textSecondary
                        font.pixelSize: 11
                        width: root.colCreated
                        elide: Text.ElideRight
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onClicked: (mouse) => {
                        if (mouse.button === Qt.RightButton) {
                            // If the right-clicked row is part of a
                            // multi-selection, leave the selection
                            // alone — the menu's targetId still points
                            // at this row (single-job menu items will
                            // operate on it). Bulk ops still come from
                            // the action strip.
                            if (!root.isChecked(jobId)) {
                                const next = {}
                                next[jobId] = true
                                root.checked = next
                                appBridge.currentJobId = jobId
                            }
                            jobMenu.targetId = jobId
                            jobMenu.targetState = state
                            jobMenu.popup()
                            mouse.accepted = true
                            return
                        }
                        root.handleRowClick(jobId, mouse)
                        mouse.accepted = true
                    }
                }

                // Drag-to-reorder handle. Declared after the row MouseArea
                // so it wins the left 22px strip. The row never physically
                // moves during the drag — an indicator line marks the drop
                // gap and the backend refresh applies the new order.
                MouseArea {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: root.colHandle
                    preventStealing: true
                    cursorShape: root.dragJobId !== ""
                                 ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                    onPressed: root.beginDrag(jobId, index)
                    onPositionChanged: (mouse) => {
                        const p = mapToItem(jobList.contentItem, mouse.x, mouse.y)
                        root.updateDrag(p.y)
                    }
                    onReleased: root.endDrag()
                    onCanceled: { root.dropGap = -1; root.endDrag() }
                }
            }

            // Insertion indicator for drag-to-reorder. Declared children
            // of a Flickable are reparented into contentItem, so y is in
            // content coordinates (scrolls with the rows).
            Rectangle {
                visible: root.dropGap >= 0
                x: 0
                y: root.dropGap * 32 - 1
                width: jobList.width - Theme.scrollBarWidth
                height: 2
                color: Theme.accent
                z: 10
            }
        }
    }

    // Right-click context menu. When more than one row is selected
    // every action loops over the selection via applyToChecked (same
    // path the bulk-action strip uses). When exactly one row is
    // selected the action targets just it. Either way state is
    // mutated through the same single-job AppBridge methods, so the
    // menu and the strip stay behavioural twins.
    Menu {
        id: jobMenu
        property string targetId: ""
        property string targetState: ""

        // Suffix the labels with a count when operating on a selection,
        // e.g. "Pause 3 jobs". Single-job case (count===1) gets the
        // bare verb. Computed once per popup so the label doesn't churn.
        function bulkSuffix() {
            const n = root.checkedCount()
            return n > 1 ? qsTr(" %1 jobs").arg(n) : ""
        }

        MenuItem {
            text: qsTr("Pause") + jobMenu.bulkSuffix()
            // For multi, enabled if any selected row could be paused —
            // we don't pre-walk the selection (cheap-ish but adds code);
            // server-side no-ops on already-paused/completed jobs, so
            // a bulk Pause across mixed states is safe.
            enabled: root.checkedCount() > 1
                     || jobMenu.targetState === "active"
            onTriggered: root.applyToChecked(
                function(id) { appBridge.pauseJob(id) })
        }
        MenuItem {
            text: qsTr("Resume") + jobMenu.bulkSuffix()
            enabled: root.checkedCount() > 1
                     || jobMenu.targetState === "paused"
            onTriggered: root.applyToChecked(
                function(id) { appBridge.resumeJob(id) })
        }
        MenuSeparator {}
        MenuItem {
            // Single-job only: the edit form is one manifest, one job.
            text: appBridge.leaderSupportsJobEdit
                  ? qsTr("Edit Job…")
                  : qsTr("Edit Job… (needs leader upgrade)")
            enabled: root.checkedCount() <= 1
                     && appBridge.leaderSupportsJobEdit
            onTriggered: appBridge.openJobEditor(jobMenu.targetId)
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Retry failed chunks") + jobMenu.bulkSuffix()
            onTriggered: root.applyToChecked(
                function(id) { appBridge.retryFailedChunks(id) })
        }
        MenuItem {
            text: qsTr("Requeue") + jobMenu.bulkSuffix()
            onTriggered: root.applyToChecked(
                function(id) { appBridge.requeueJob(id) })
        }
        MenuSeparator {}
        // Cancel and Delete route through the bulk-confirm dialog
        // regardless of selection size — a single accidental "Delete"
        // on the wrong row is just as bad as 5, and one tap of return
        // beats unintentional data loss.
        MenuItem {
            text: qsTr("Cancel") + jobMenu.bulkSuffix()
            onTriggered: bulkConfirmDialog.openWith(
                "cancel",
                qsTr("Cancel %1 job%2?")
                    .arg(root.checkedCount())
                    .arg(root.checkedCount() === 1 ? "" : "s"),
                qsTr("This stops dispatch and immediately kills in-flight renders on all nodes. The jobs can be requeued later."),
                function(id) { appBridge.cancelJob(id) })
        }
        MenuItem {
            text: qsTr("Archive") + jobMenu.bulkSuffix()
            onTriggered: root.applyToChecked(
                function(id) { appBridge.archiveJob(id) })
        }
        MenuItem {
            text: qsTr("Delete") + jobMenu.bulkSuffix()
            onTriggered: bulkConfirmDialog.openWith(
                "delete",
                qsTr("Delete %1 job%2?")
                    .arg(root.checkedCount())
                    .arg(root.checkedCount() === 1 ? "" : "s"),
                qsTr("Removes job records and SQLite history. Output files on disk are NOT deleted. This cannot be undone."),
                function(id) { appBridge.deleteJob(id) })
        }
    }

    // Bulk action confirmation. Title + body + the per-id action are
    // all set by the bulk strip's onClicked handler before opening, so
    // this Dialog stays generic and can serve any destructive bulk op.
    Dialog {
        id: bulkConfirmDialog
        modal: true
        anchors.centerIn: parent
        width: 460
        closePolicy: Popup.NoAutoClose
        standardButtons: Dialog.NoButton

        property string actionKind: ""        // "delete" | "cancel"
        property string headerText: ""
        property string bodyText: ""
        property var    perJobAction: null    // function(jobId) — invoked per checked id

        function openWith(kind, header, body, fn) {
            actionKind = kind
            headerText = header
            bodyText = body
            perJobAction = fn
            open()
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: Theme.spacing

            Label {
                Layout.fillWidth: true
                text: bulkConfirmDialog.headerText
                color: Theme.textBright
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeLarge
                font.bold: true
                wrapMode: Text.WordWrap
            }
            Label {
                Layout.fillWidth: true
                text: bulkConfirmDialog.bodyText
                color: Theme.textMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeBase
                wrapMode: Text.WordWrap
            }

            Item { Layout.minimumHeight: Theme.padding }

            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                FlatButton {
                    text: qsTr("Cancel")
                    onClicked: bulkConfirmDialog.close()
                }
                FlatButton {
                    iconName: bulkConfirmDialog.actionKind === "delete"
                              ? "trash" : "x"
                    text: bulkConfirmDialog.actionKind === "delete"
                          ? qsTr("Delete") : qsTr("Cancel jobs")
                    variant: "danger"
                    onClicked: {
                        if (bulkConfirmDialog.perJobAction)
                            root.applyToChecked(bulkConfirmDialog.perJobAction)
                        bulkConfirmDialog.close()
                    }
                }
            }
        }
    }
}

import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MinRenderUi 1.0

Item {
    id: root

    // Persisted height of the preview section under the node list.
    Settings {
        id: nodePanelSettings
        category: "nodePanel"

        property real previewHeight: 240
    }

    function agentHealthColor(h) {
        switch (h) {
        case "ok":               return Theme.success
        case "reconnecting":     return Theme.warn
        case "needs_attention":  return Theme.error
        default:                 return Theme.textSecondary
        }
    }

    function ramString(mb) {
        if (mb <= 0) return qsTr("unknown")
        if (mb >= 1024) return (mb / 1024).toFixed(1) + qsTr(" GB")
        return mb + qsTr(" MB")
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // --- This Node section ---
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: thisNodeColumn.implicitHeight + 16
            color: Theme.surface

            ColumnLayout {
                id: thisNodeColumn
                anchors {
                    left: parent.left
                    right: parent.right
                    top: parent.top
                    margins: 8
                }
                spacing: 6

                // The hostname and LEADER badge live in the window's title
                // row (Main.qml); this card is the detail.
                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: 8
                    rowSpacing: 2

                    Label { text: qsTr("Host");     color: Theme.textMuted; font.pixelSize: Theme.fontSizeBase }
                    Label {
                        text: appBridge.thisNodeHostname
                        color: Theme.textPrimary; font.pixelSize: Theme.fontSizeBase
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }

                    Label { text: qsTr("Node ID");  color: Theme.textMuted; font.pixelSize: Theme.fontSizeBase }
                    Label {
                        text: appBridge.thisNodeId
                        color: "#bbbbbb"; font.pixelSize: 11
                        font.family: Theme.monoFamily
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                    }

                    Label { text: qsTr("GPU");      color: Theme.textMuted; font.pixelSize: Theme.fontSizeBase }
                    Label {
                        text: appBridge.thisNodeGpu.length > 0
                              ? appBridge.thisNodeGpu : qsTr("—")
                        color: Theme.textPrimary; font.pixelSize: Theme.fontSizeBase
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }

                    Label { text: qsTr("CPU / RAM");color: Theme.textMuted; font.pixelSize: Theme.fontSizeBase }
                    Label {
                        text: qsTr("%1 cores · %2")
                              .arg(appBridge.thisNodeCpuCores)
                              .arg(root.ramString(appBridge.thisNodeRamMb))
                        color: Theme.textPrimary; font.pixelSize: Theme.fontSizeBase
                        Layout.fillWidth: true
                    }

                    Label { text: qsTr("Tags");     color: Theme.textMuted; font.pixelSize: Theme.fontSizeBase }
                    Label {
                        text: appBridge.tagsCsv.length > 0
                              ? appBridge.tagsCsv : qsTr("—")
                        color: Theme.textPrimary; font.pixelSize: Theme.fontSizeBase
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }

                // Suspension warning. The leader benches a node after 5
                // chunk failures in 5 minutes, and the suspension never
                // expires on its own, so without this the node looks
                // healthy while silently receiving no work forever.
                Rectangle {
                    visible: appBridge.thisNodeSuspended
                    Layout.fillWidth: true
                    Layout.topMargin: 4
                    Layout.preferredHeight: suspendColumn.implicitHeight + 12
                    color: Qt.darker(Theme.error, 3.0)
                    border.width: 1
                    border.color: Theme.error
                    radius: Theme.radiusSmall

                    ColumnLayout {
                        id: suspendColumn
                        anchors {
                            left: parent.left
                            right: parent.right
                            top: parent.top
                            margins: 6
                        }
                        spacing: 4

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            Icon {
                                name: "warning"
                                size: 14
                                color: Theme.error
                                Layout.alignment: Qt.AlignVCenter
                            }
                            Label {
                                text: qsTr("SUSPENDED")
                                color: Theme.error
                                font.bold: true
                                font.pixelSize: Theme.fontSizeBase
                            }
                        }

                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            font.pixelSize: Theme.fontSizeSmall
                            color: Theme.textSecondary
                            text: qsTr("The leader benched this node after %1 chunk failures within 5 minutes. It will not be given work until unsuspended, and the suspension does not clear on its own.")
                                  .arg(appBridge.thisNodeSuspendFailures)
                        }

                        FlatButton {
                            Layout.fillWidth: true
                            iconName: "arrow-counter-clockwise"
                            text: qsTr("Unsuspend this node")
                            variant: "danger"
                            onClicked: appBridge.unsuspendThisNode()
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 4
                    spacing: Theme.spacing

                    FlatButton {
                        Layout.fillWidth: true
                        iconName: appBridge.thisNodeActive ? "pause" : "play"
                        text: appBridge.thisNodeActive
                              ? qsTr("Stop") : qsTr("Resume")
                        variant: appBridge.thisNodeActive ? "default" : "primary"
                        onClicked: appBridge.toggleNodeActive()
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.border
        }

        // --- Peers header ---
        PanelHeader {
            Layout.fillWidth: true
            title: qsTr("Peers")
            subtitle: qsTr("%1 online").arg(peerList.count)
        }

        // --- Peers ListView ---
        ListView {
            id: peerList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: appBridge.nodesModel
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: MrScrollBar {}

            // Multi-select (components/ListSelection.qml), keyed by node
            // id. Visual plus the context menu's target set: Stop / Start /
            // Restart / Unsuspend act on every selected node they apply to.
            ListSelection {
                id: peerSel
                rowCount: peerList.count
                keyAt: function(row) { return appBridge.nodesModel.rowAt(row).nodeId }
                pageRows: Math.max(1, Math.floor(peerList.height / 52) - 1)
                onMoved: (row) => peerList.positionViewAtIndex(row, ListView.Contain)
            }
            function selectedPeerRows() {
                const out = []
                for (let i = 0; i < peerList.count; ++i) {
                    const r = appBridge.nodesModel.rowAt(i)
                    if (peerSel.isSelected(r.nodeId)) out.push(r)
                }
                return out
            }
            // Right-click: alive targets get the live menu (dead ones in a
            // mixed selection are skipped); an all-dead selection gets the
            // filesystem-signal menu.
            function openMenuForSelection() {
                const rows = selectedPeerRows()
                const live = rows.filter(r => r.isAlive)
                if (live.length > 0) {
                    livePeerMenu.rows = live
                    livePeerMenu.popup()
                } else if (rows.length > 0) {
                    deadPeerMenu.rows = rows
                    deadPeerMenu.popup()
                }
            }

            keyNavigationEnabled: false
            activeFocusOnTab: true
            Keys.onPressed: (event) => {
                if (peerSel.handleKey(event)) event.accepted = true
            }
            footer: Item {
                width: peerList.width
                height: Math.max(0, peerList.height - peerList.count * 52)
                MouseArea {
                    anchors.fill: parent
                    onClicked: { peerList.forceActiveFocus(); peerSel.clear() }
                }
            }

            delegate: Rectangle {
                required property string nodeId
                required property string hostname
                required property bool   isLeader
                required property bool   isActive
                required property bool   isAlive
                required property string agentHealth
                required property string alertReason
                required property var    tags
                required property string renderState
                required property string activeJob
                required property bool   isSuspended
                required property int    suspendFailures
                required property int    index

                width: peerList.width
                height: 52
                color: peerSel.isSelected(nodeId)
                       ? Theme.selection
                       : (index % 2 === 0 ? Theme.bgAlt : Theme.surface)
                Behavior on color { ColorAnimation { duration: Theme.hoverMs } }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: Theme.padding + Theme.scrollBarWidth
                    spacing: 10

                    // Health dot. Suspension outranks agent health: a
                    // suspended node keeps reporting "ok" (the leader's
                    // verdict is invisible to the node itself), so
                    // without this it would sit here green and idle
                    // while never being given work.
                    Rectangle {
                        Layout.alignment: Qt.AlignVCenter
                        width: 10; height: 10; radius: 5
                        color: isSuspended
                               ? Theme.error
                               : (!isAlive
                                  ? Theme.textMuted
                                  : root.agentHealthColor(agentHealth))
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter
                        spacing: 2

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            Label {
                                text: hostname.length > 0
                                      ? hostname : nodeId
                                color: isAlive ? Theme.textPrimary : Theme.textMuted
                                font.pixelSize: Theme.fontSizeBase
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Label {
                                visible: isLeader
                                text: qsTr("LEADER")
                                color: Theme.accent
                                font.pixelSize: 9
                                font.bold: true
                            }
                            Icon {
                                visible: isSuspended
                                name: "warning"
                                size: 12
                                color: Theme.error
                                Layout.alignment: Qt.AlignVCenter
                            }
                        }

                        Label {
                            text: isSuspended
                                  ? qsTr("suspended · %1 failures · no work assigned")
                                    .arg(suspendFailures)
                                  : (!isAlive
                                     ? qsTr("offline")
                                     : (renderState === "rendering"
                                        ? qsTr("rendering · %1").arg(activeJob)
                                        : (isActive ? qsTr("idle") : qsTr("stopped"))))
                            color: isSuspended ? Theme.error : Theme.textSecondary
                            font.pixelSize: Theme.fontSizeSmall
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onClicked: (mouse) => {
                        peerList.forceActiveFocus()
                        if (mouse.button === Qt.RightButton) {
                            peerSel.handleContextClick(index)
                            peerList.openMenuForSelection()
                            return
                        }
                        peerSel.handleClick(index, mouse)
                    }
                }
            }

            // Two Menus rather than one with visible:false items — Qt
            // Quick Menu reserves layout space for hidden MenuItem
            // children, leaving gaps at popup time. Choose at popup.
            ThemedMenu {
                id: livePeerMenu
                // The alive nodes of the selection, set at popup.
                property var rows: []
                readonly property int targetCount: rows.length
                readonly property int activeCount: rows.filter(r => r.isActive).length
                readonly property int suspendedCount: rows.filter(r => r.isSuspended).length
                function suffix(n) { return targetCount > 1 ? qsTr(" (%1)").arg(n) : "" }

                // Non-interactive header naming the target node(s).
                ThemedMenuItem {
                    enabled: false
                    text: livePeerMenu.targetCount === 1
                          ? livePeerMenu.rows[0].nodeId
                          : qsTr("%1 nodes").arg(livePeerMenu.targetCount)
                    font.family: Theme.monoFamily
                    font.pixelSize: Theme.fontSizeSmall
                }
                ThemedMenuSeparator {}
                ThemedMenuItem {
                    text: qsTr("Stop") + livePeerMenu.suffix(livePeerMenu.activeCount)
                    enabled: livePeerMenu.activeCount > 0
                    onTriggered: {
                        for (const r of livePeerMenu.rows)
                            if (r.isActive) appBridge.setPeerNodeActive(r.nodeId, false)
                    }
                }
                ThemedMenuItem {
                    text: qsTr("Start") + livePeerMenu.suffix(livePeerMenu.targetCount - livePeerMenu.activeCount)
                    enabled: livePeerMenu.activeCount < livePeerMenu.targetCount
                    onTriggered: {
                        for (const r of livePeerMenu.rows)
                            if (!r.isActive) appBridge.setPeerNodeActive(r.nodeId, true)
                    }
                }
                ThemedMenuItem {
                    text: qsTr("Restart App") + livePeerMenu.suffix(livePeerMenu.targetCount)
                    onTriggered: {
                        for (const r of livePeerMenu.rows) appBridge.restartPeerApp(r.nodeId)
                    }
                }
                // Disabled rather than hidden when no target is
                // suspended, so the row still reads as "not suspended"
                // instead of the item silently vanishing.
                ThemedMenuItem {
                    text: qsTr("Unsuspend") + livePeerMenu.suffix(livePeerMenu.suspendedCount)
                    enabled: livePeerMenu.suspendedCount > 0
                    onTriggered: {
                        for (const r of livePeerMenu.rows)
                            if (r.isSuspended) appBridge.unsuspendNode(r.nodeId)
                    }
                }
            }

            ThemedMenu {
                id: deadPeerMenu
                property var rows: []
                readonly property int targetCount: rows.length

                ThemedMenuItem {
                    enabled: false
                    text: deadPeerMenu.targetCount === 1
                          ? deadPeerMenu.rows[0].nodeId
                          : qsTr("%1 offline nodes").arg(deadPeerMenu.targetCount)
                    font.family: Theme.monoFamily
                    font.pixelSize: Theme.fontSizeSmall
                }
                ThemedMenuSeparator {}
                // Writes an empty `restart` file under {farmPath}/nodes/{id}/
                // so the peer's filesystem watcher triggers a local restart
                // next time it comes back online.
                ThemedMenuItem {
                    text: qsTr("Write restart signal (filesystem)")
                    onTriggered: {
                        for (const r of deadPeerMenu.rows) appBridge.writePeerRestartSignal(r.nodeId)
                    }
                }
            }
        }

        // --- Latest-frame preview for the selected job ---
        // Lives here (not in Job Detail) so the frame grid keeps the
        // full detail width and the preview stays visible even when
        // the Logs tab is in front. Hidden when nothing is selected or
        // the build has no image libs — the node list reclaims the room.
        Rectangle {
            visible: appBridge.previewSupported
                     && appBridge.currentJobId.length > 0
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.toolStripHeight
            color: Theme.toolbar

            Rectangle {
                anchors.left:   parent.left
                anchors.right:  parent.right
                anchors.top:    parent.top
                height: Theme.dividerWidth
                color: Theme.divider
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.padding
                spacing: 6

                Label {
                    text: qsTr("Preview")
                    color: Theme.textBright
                    font.family: Theme.fontFamily
                    font.bold: true
                    font.pixelSize: Theme.fontSizeBase
                }
                Label {
                    Layout.fillWidth: true
                    text: "· " + appBridge.currentJobId
                    color: Theme.textMuted
                    font.family: Theme.monoFamily
                    font.pixelSize: Theme.fontSizeBase
                    elide: Text.ElideMiddle
                }
            }
        }

        JobPreview {
            visible: appBridge.previewSupported
                     && appBridge.currentJobId.length > 0
            jobId: appBridge.currentJobId
            Layout.fillWidth: true
            Layout.preferredHeight: nodePanelSettings.previewHeight
        }
    }
}

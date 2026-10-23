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

                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        text: qsTr("This Node")
                        font.bold: true
                        color: Theme.textPrimary
                    }
                    Item { Layout.fillWidth: true }
                    StatusBadge {
                        visible: appBridge.thisNodeIsLeader
                        text: qsTr("LEADER")
                    }
                }

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

            // Click-to-select (single). Purely visual — actions still
            // come from the context menu — but anchors the menu to a
            // visibly selected row.
            property string selectedNodeId: ""

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
                color: peerList.selectedNodeId === nodeId
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
                        peerList.selectedNodeId = nodeId
                        if (mouse.button !== Qt.RightButton)
                            return
                        if (isAlive)
                        {
                            livePeerMenu.targetNodeId    = nodeId
                            livePeerMenu.targetIsActive  = isActive
                            livePeerMenu.targetSuspended = isSuspended
                            livePeerMenu.popup()
                        }
                        else
                        {
                            deadPeerMenu.targetNodeId = nodeId
                            deadPeerMenu.popup()
                        }
                    }
                }
            }

            // Two Menus rather than one with visible:false items — Qt
            // Quick Menu reserves layout space for hidden MenuItem
            // children, leaving gaps at popup time. Choose at popup.
            ThemedMenu {
                id: livePeerMenu
                property string targetNodeId:    ""
                property bool   targetIsActive:  false
                property bool   targetSuspended: false

                // Non-interactive header naming the target node.
                ThemedMenuItem {
                    enabled: false
                    text: livePeerMenu.targetNodeId
                    font.family: Theme.monoFamily
                    font.pixelSize: Theme.fontSizeSmall
                }
                ThemedMenuSeparator {}
                ThemedMenuItem {
                    text: livePeerMenu.targetIsActive ? qsTr("Stop") : qsTr("Start")
                    onTriggered: appBridge.setPeerNodeActive(
                        livePeerMenu.targetNodeId, !livePeerMenu.targetIsActive)
                }
                ThemedMenuItem {
                    text: qsTr("Restart App")
                    onTriggered: appBridge.restartPeerApp(livePeerMenu.targetNodeId)
                }
                // Disabled rather than hidden when the node is not
                // suspended, so the row still reads as "not suspended"
                // instead of the item silently vanishing.
                ThemedMenuItem {
                    text: qsTr("Unsuspend")
                    enabled: livePeerMenu.targetSuspended
                    onTriggered: appBridge.unsuspendNode(livePeerMenu.targetNodeId)
                }
            }

            ThemedMenu {
                id: deadPeerMenu
                property string targetNodeId: ""

                ThemedMenuItem {
                    enabled: false
                    text: deadPeerMenu.targetNodeId
                    font.family: Theme.monoFamily
                    font.pixelSize: Theme.fontSizeSmall
                }
                ThemedMenuSeparator {}
                // Writes an empty `restart` file under {farmPath}/nodes/{id}/
                // so the peer's filesystem watcher triggers a local restart
                // next time it comes back online.
                ThemedMenuItem {
                    text: qsTr("Write restart signal (filesystem)")
                    onTriggered: appBridge.writePeerRestartSignal(deadPeerMenu.targetNodeId)
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

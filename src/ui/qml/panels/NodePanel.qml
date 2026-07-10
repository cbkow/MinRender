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
                required property int    index

                width: peerList.width
                height: 52
                color: peerList.selectedNodeId === nodeId
                       ? Theme.selection
                       : (index % 2 === 0 ? Theme.bgAlt : Theme.surface)

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: Theme.padding + Theme.scrollBarWidth
                    spacing: 10

                    // Health dot
                    Rectangle {
                        Layout.alignment: Qt.AlignVCenter
                        width: 10; height: 10; radius: 5
                        color: !isAlive
                               ? Theme.textMuted
                               : root.agentHealthColor(agentHealth)
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
                        }

                        Label {
                            text: !isAlive
                                  ? qsTr("offline")
                                  : (renderState === "rendering"
                                     ? qsTr("rendering · %1").arg(activeJob)
                                     : (isActive ? qsTr("idle") : qsTr("stopped")))
                            color: Theme.textSecondary
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
                            livePeerMenu.targetNodeId   = nodeId
                            livePeerMenu.targetIsActive = isActive
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
            Menu {
                id: livePeerMenu
                property string targetNodeId:   ""
                property bool   targetIsActive: false

                // Non-interactive header naming the target node.
                MenuItem {
                    enabled: false
                    text: livePeerMenu.targetNodeId
                    font.family: Theme.monoFamily
                    font.pixelSize: Theme.fontSizeSmall
                }
                MenuSeparator {}
                MenuItem {
                    text: livePeerMenu.targetIsActive ? qsTr("Stop") : qsTr("Start")
                    onTriggered: appBridge.setPeerNodeActive(
                        livePeerMenu.targetNodeId, !livePeerMenu.targetIsActive)
                }
                MenuItem {
                    text: qsTr("Restart App")
                    onTriggered: appBridge.restartPeerApp(livePeerMenu.targetNodeId)
                }
                MenuItem {
                    text: qsTr("Unsuspend")
                    onTriggered: appBridge.unsuspendNode(livePeerMenu.targetNodeId)
                }
            }

            Menu {
                id: deadPeerMenu
                property string targetNodeId: ""

                MenuItem {
                    enabled: false
                    text: deadPeerMenu.targetNodeId
                    font.family: Theme.monoFamily
                    font.pixelSize: Theme.fontSizeSmall
                }
                MenuSeparator {}
                // Writes an empty `restart` file under {farmPath}/nodes/{id}/
                // so the peer's filesystem watcher triggers a local restart
                // next time it comes back online.
                MenuItem {
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

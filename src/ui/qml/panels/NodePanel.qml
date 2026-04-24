import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MinRenderUi 1.0

Item {
    id: root

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
                    spacing: 6

                    Button {
                        text: appBridge.thisNodeActive
                              ? qsTr("Stop") : qsTr("Resume")
                        Layout.fillWidth: true
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
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

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
                color: index % 2 === 0 ? Theme.bgAlt : Theme.surface

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
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
                    acceptedButtons: Qt.RightButton
                    onClicked: (mouse) => {
                        peerMenu.targetNodeId = nodeId
                        peerMenu.targetIsAlive = isAlive
                        peerMenu.popup()
                    }
                }
            }

            Menu {
                id: peerMenu
                property string targetNodeId: ""
                property bool   targetIsAlive: false

                MenuItem {
                    text: qsTr("Unsuspend")
                    enabled: peerMenu.targetNodeId.length > 0
                    onTriggered: appBridge.unsuspendNode(peerMenu.targetNodeId)
                }
            }
        }
    }
}

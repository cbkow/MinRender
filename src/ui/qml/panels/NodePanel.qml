import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    function agentHealthColor(h) {
        switch (h) {
        case "ok":               return "#9ece6a"
        case "reconnecting":     return "#e0af68"
        case "needs_attention":  return "#f7768e"
        default:                 return "#888"
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
            color: "#1a1a1a"

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
                        color: "#cccccc"
                    }
                    Item { Layout.fillWidth: true }
                    Rectangle {
                        visible: appBridge.thisNodeIsLeader
                        color: "#3c5b8a"
                        radius: 3
                        implicitWidth: leaderLabel.implicitWidth + 10
                        implicitHeight: 16
                        Label {
                            id: leaderLabel
                            anchors.centerIn: parent
                            text: qsTr("LEADER")
                            font.pixelSize: 10
                            font.bold: true
                            color: "#e6e6e6"
                        }
                    }
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: 8
                    rowSpacing: 2

                    Label { text: qsTr("Host");     color: "#777"; font.pixelSize: 11 }
                    Label {
                        text: appBridge.thisNodeHostname
                        color: "#cccccc"; font.pixelSize: 11
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }

                    Label { text: qsTr("Node ID");  color: "#777"; font.pixelSize: 11 }
                    Label {
                        text: appBridge.thisNodeId
                        color: "#bbbbbb"; font.pixelSize: 11
                        font.family: "monospace"
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                    }

                    Label { text: qsTr("GPU");      color: "#777"; font.pixelSize: 11 }
                    Label {
                        text: appBridge.thisNodeGpu.length > 0
                              ? appBridge.thisNodeGpu : qsTr("—")
                        color: "#cccccc"; font.pixelSize: 11
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }

                    Label { text: qsTr("CPU / RAM");color: "#777"; font.pixelSize: 11 }
                    Label {
                        text: qsTr("%1 cores · %2")
                              .arg(appBridge.thisNodeCpuCores)
                              .arg(root.ramString(appBridge.thisNodeRamMb))
                        color: "#cccccc"; font.pixelSize: 11
                        Layout.fillWidth: true
                    }

                    Label { text: qsTr("Tags");     color: "#777"; font.pixelSize: 11 }
                    Label {
                        text: appBridge.tagsCsv.length > 0
                              ? appBridge.tagsCsv : qsTr("—")
                        color: "#cccccc"; font.pixelSize: 11
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
            color: "#2a2a2a"
        }

        // --- Peers header ---
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 24
            color: "#161616"
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                Label {
                    text: qsTr("Peers")
                    color: "#999"
                    font.pixelSize: 11
                    font.bold: true
                }
                Item { Layout.fillWidth: true }
                Label {
                    text: qsTr("%1 online").arg(peerList.count)
                    color: "#666"
                    font.pixelSize: 11
                }
            }
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
                color: index % 2 === 0 ? "#181818" : "#1c1c1c"

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
                               ? "#555"
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
                                color: isAlive ? "#cccccc" : "#777"
                                font.pixelSize: 12
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Label {
                                visible: isLeader
                                text: qsTr("LEADER")
                                color: "#7aa2f7"
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
                            color: "#888"
                            font.pixelSize: 10
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

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MinRenderUi 1.0

Item {
    id: root

    // Filter chips. Level strings match MonitorLog::Entry::level values
    // ("INFO", "WARN", "ERROR"). Anything else falls through unfiltered.
    property bool showInfo: true
    property bool showWarn: true
    property bool showError: true
    property bool autoscroll: true

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

                ToolButton {
                    text: qsTr("Info")
                    checkable: true
                    checked: root.showInfo
                    onToggled: root.showInfo = checked
                }
                ToolButton {
                    text: qsTr("Warn")
                    checkable: true
                    checked: root.showWarn
                    onToggled: root.showWarn = checked
                }
                ToolButton {
                    text: qsTr("Error")
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
                    // logList.count is signal-driven; appBridge.logModel
                    // doesn't expose a QML property that changes on insert.
                    text: qsTr("%1 entries").arg(logList.count)
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSizeSmall
                }

                ToolSeparator { Layout.fillHeight: true }

                Button {
                    text: qsTr("Clear")
                    flat: true
                    onClicked: appBridge.logModel.clear()
                }
            }
        }

        // --- Log list ---
        ListView {
            id: logList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: appBridge.logModel
            boundsBehavior: Flickable.StopAtBounds
            reuseItems: true
            cacheBuffer: 400

            // Autoscroll when new entries arrive, but only if the user
            // hasn't scrolled away from the bottom. contentY==0 means the
            // list is at the end (positionViewAtEnd puts it there), and
            // we interpret "near the end" generously.
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
                // Filter by setting height to 0; cheap enough for 5 k rows.
                // Proper QSortFilterProxyModel is a later optimization.
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
                        // Fill the remainder after the three fixed columns
                        // (84 + 48 + 120 + 3*8 spacing = 276).
                        width: parent.width - 276
                        wrapMode: Text.Wrap
                    }
                }
            }
        }
    }
}

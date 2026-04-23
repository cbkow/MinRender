import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: window

    width: 1440
    height: 900
    visible: true
    title: qsTr("MinRender Monitor")

    // Hide to tray on close. The user quits via tray → Exit (which calls
    // QCoreApplication::quit via main_qt.cpp) or File → Exit menu item.
    // main_qt.cpp sets setQuitOnLastWindowClosed(false), so hiding the
    // window keeps the app alive.
    onClosing: (close) => {
        close.accepted = false
        window.visible = false
    }

    // Persisted split sizes + panel visibility across launches.
    // QtCore's Settings resolves to the same %LOCALAPPDATA%/MinRender
    // location QCoreApplication::setOrganizationName("MinRender") establishes
    // in main_qt.cpp, so this piggybacks on the existing config root.
    Settings {
        id: panelSettings
        category: "panels"

        property real nodePanelWidth: window.width * 0.28
        property real jobListHeight: (window.height - menuBarArea.height) * 0.33
        property real jobDetailWidth: window.width * 0.36

        property bool nodePanelVisible: true
        property bool jobListVisible: true
        property bool jobDetailVisible: true
        property bool logVisible: true
    }

    menuBar: MenuBar {
        id: menuBarArea

        Menu {
            title: qsTr("&File")
            Action { text: qsTr("Settings…");     onTriggered: settingsDialog.open() }
            Action { text: qsTr("Farm Cleanup…"); onTriggered: console.log("[Menu] File → Farm Cleanup (Phase 4)") }
            MenuSeparator {}
            Action { text: qsTr("E&xit"); onTriggered: Qt.quit() }
        }

        Menu {
            title: qsTr("&Jobs")
            Action { text: qsTr("New Job…"); onTriggered: console.log("[Menu] Jobs → New Job (Phase 4)") }
        }

        Menu {
            title: qsTr("&View")
            MenuItem {
                text: qsTr("Node Panel")
                checkable: true
                checked: panelSettings.nodePanelVisible
                onToggled: panelSettings.nodePanelVisible = checked
            }
            MenuItem {
                text: qsTr("Job List")
                checkable: true
                checked: panelSettings.jobListVisible
                onToggled: panelSettings.jobListVisible = checked
            }
            MenuItem {
                text: qsTr("Job Detail")
                checkable: true
                checked: panelSettings.jobDetailVisible
                onToggled: panelSettings.jobDetailVisible = checked
            }
            MenuItem {
                text: qsTr("Log")
                checkable: true
                checked: panelSettings.logVisible
                onToggled: panelSettings.logVisible = checked
            }
        }

        Menu {
            title: qsTr("&Help")
            Action { text: qsTr("Guide");               onTriggered: console.log("[Menu] Help → Guide (Phase 4)") }
            Action { text: qsTr("Check for Updates…"); onTriggered: console.log("[Menu] Help → Updates (Phase 2)") }
            MenuSeparator {}
            Action {
                text: qsTr("MinRender %1").arg(Qt.application.version)
                enabled: false
            }
        }
    }

    // Outer horizontal split: NodePanel on the left, everything else on the right.
    SplitView {
        id: outerSplit
        anchors.fill: parent
        orientation: Qt.Horizontal

        Rectangle {
            id: nodePanelPlaceholder
            visible: panelSettings.nodePanelVisible
            color: "#1e1e1e"
            SplitView.preferredWidth: panelSettings.nodePanelWidth
            SplitView.minimumWidth: 220
            onWidthChanged: if (SplitView.view) panelSettings.nodePanelWidth = width
            Label {
                anchors.centerIn: parent
                text: qsTr("Node Panel (Phase 4)")
                color: "#888"
            }
        }

        // Right side: vertical split — JobList on top, JobDetail+Log below.
        SplitView {
            orientation: Qt.Vertical
            SplitView.fillWidth: true

            Rectangle {
                id: jobListPlaceholder
                visible: panelSettings.jobListVisible
                color: "#232323"
                SplitView.preferredHeight: panelSettings.jobListHeight
                SplitView.minimumHeight: 120
                onHeightChanged: if (SplitView.view) panelSettings.jobListHeight = height
                Label {
                    anchors.centerIn: parent
                    text: qsTr("Job List (Phase 4)")
                    color: "#888"
                }
            }

            // Bottom pane: horizontal split — JobDetail on left, Log on right.
            SplitView {
                orientation: Qt.Horizontal
                SplitView.fillHeight: true

                Rectangle {
                    id: jobDetailPlaceholder
                    visible: panelSettings.jobDetailVisible
                    color: "#1a1a1a"
                    SplitView.preferredWidth: panelSettings.jobDetailWidth
                    SplitView.minimumWidth: 240
                    onWidthChanged: if (SplitView.view) panelSettings.jobDetailWidth = width
                    Label {
                        anchors.centerIn: parent
                        text: qsTr("Job Detail (Phase 5)")
                        color: "#888"
                    }
                }

                LogPanel {
                    id: logPanelInstance
                    visible: panelSettings.logVisible
                    SplitView.fillWidth: true
                    SplitView.minimumWidth: 240
                }
            }
        }
    }

    // --- Settings dialog ---
    // Loader with `active: settingsDialog.visible` destroys and re-creates
    // the SettingsPanel each time the dialog opens. That way binding
    // breakage from previous edits doesn't persist, and an external revert
    // always shows in the fields on the next open.
    //
    // closePolicy is NoAutoClose — the panel's Save/Cancel are the only
    // exits, so we never leave the dialog with unsaved in-memory config
    // edits hanging on MonitorApp::config().
    Dialog {
        id: settingsDialog
        title: qsTr("Settings")
        modal: true
        anchors.centerIn: parent
        width: 600
        height: Math.min(760, window.height - 80)
        closePolicy: Popup.NoAutoClose

        Loader {
            anchors.fill: parent
            active: settingsDialog.visible
            sourceComponent: settingsPanelComponent
        }

        Component {
            id: settingsPanelComponent
            SettingsPanel {
                onAccepted: settingsDialog.close()
                onRejected: settingsDialog.close()
            }
        }
    }
}

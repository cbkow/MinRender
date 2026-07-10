import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MinRenderUi 1.0

ApplicationWindow {
    id: window

    width: 1440
    height: 900
    visible: true
    title: qsTr("minRender Monitor")

    // QML controls (TextField/ComboBox/SpinBox/CheckBox/MenuBar) read
    // their colours from the inherited palette, NOT the Theme singleton
    // and NOT QApplication::setPalette (which only reaches QtWidgets).
    // Setting roles here propagates to every child control so inputs
    // render against Theme.surface with Theme.textPrimary text rather
    // than Fusion's default near-white-on-white.
    palette.window:          Theme.bg
    palette.windowText:      Theme.textPrimary
    palette.base:            Theme.surface
    palette.alternateBase:   Theme.surfaceAlt
    palette.text:            Theme.textPrimary
    palette.placeholderText: Theme.textMuted
    palette.button:          Theme.surfaceAlt
    palette.buttonText:      Theme.textPrimary
    palette.highlight:       Theme.accent
    palette.highlightedText: Theme.textBright
    palette.toolTipBase:     Theme.surfaceAlt
    palette.toolTipText:     Theme.textPrimary
    palette.mid:             Theme.borderStrong
    palette.dark:            Theme.textSecondary

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
        // Active tab in the bottom pane: 0 = Job Detail, 1 = Logs.
        property int detailTabIndex: 0

        property bool nodePanelVisible: true
        property bool jobListVisible: true
    }

    menuBar: MenuBar {
        id: menuBarArea

        Menu {
            title: qsTr("&File")
            Action { text: qsTr("Settings…");     onTriggered: settingsDialog.open() }
            Action { text: qsTr("Farm Cleanup…"); onTriggered: farmCleanupDialog.open() }
            MenuSeparator {}
            Action { text: qsTr("E&xit"); onTriggered: Qt.quit() }
        }

        Menu {
            title: qsTr("&Jobs")
            Action { text: qsTr("New Job…"); onTriggered: appBridge.submissionMode = true }
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
        }

        Menu {
            title: qsTr("&About")
            // Version line stays disabled — it's a label, not an action.
            // Qt.application.version is fed by main_qt.cpp's
            // QCoreApplication::setApplicationVersion, which reads the
            // CMake PROJECT_VERSION at compile time.
            Action {
                text: qsTr("minRender v%1").arg(Qt.application.version)
                enabled: false
            }
            MenuSeparator {}
            Action {
                text: qsTr("Check for &Updates…")
                onTriggered: appUpdater.checkForUpdates()
            }
            Action {
                text: qsTr("Docs")
                onTriggered: Qt.openUrlExternally("https://minrender.com/")
            }
        }
    }

    // Outer horizontal split: NodePanel on the left, everything else on the right.
    SplitView {
        id: outerSplit
        anchors.fill: parent
        orientation: Qt.Horizontal

        NodePanel {
            id: nodePanelInstance
            visible: panelSettings.nodePanelVisible
            SplitView.preferredWidth: panelSettings.nodePanelWidth
            SplitView.minimumWidth: 260
            onWidthChanged: if (SplitView.view) panelSettings.nodePanelWidth = width
        }

        // Right side: vertical split — JobList on top, JobDetail+Log below.
        SplitView {
            orientation: Qt.Vertical
            SplitView.fillWidth: true

            JobListPanel {
                id: jobListInstance
                visible: panelSettings.jobListVisible
                SplitView.preferredHeight: panelSettings.jobListHeight
                SplitView.minimumHeight: 120
                onHeightChanged: if (SplitView.view) panelSettings.jobListHeight = height
                onNewJobRequested: appBridge.submissionMode = true
            }

            // Bottom pane: tabbed — Job Detail and Logs share the full
            // width. Both panels stay instantiated (StackLayout hides,
            // never unloads), so log tailing and the 3 s chunk refresh
            // keep running while the other tab is in front.
            Rectangle {
                SplitView.fillHeight: true
                color: Theme.bg

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0

                    // Tab strip — same toolbar idiom as PanelHeader: flat
                    // bar, 1px bottom divider, accent underline on the
                    // active tab.
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.toolStripHeight
                        color: Theme.toolbar

                        Rectangle {
                            anchors.left:   parent.left
                            anchors.right:  parent.right
                            anchors.bottom: parent.bottom
                            height: Theme.dividerWidth
                            color: Theme.divider
                        }

                        Row {
                            anchors.fill: parent
                            anchors.leftMargin: Theme.padding
                            spacing: 0

                            Repeater {
                                model: [qsTr("Job Detail"), qsTr("Logs")]

                                delegate: Item {
                                    required property string modelData
                                    required property int index

                                    readonly property bool active:
                                        panelSettings.detailTabIndex === index

                                    width: tabLabel.implicitWidth + Theme.padding * 2
                                    height: parent.height

                                    Label {
                                        id: tabLabel
                                        anchors.centerIn: parent
                                        text: modelData
                                        color: active ? Theme.textBright
                                             : tabMouse.containsMouse ? Theme.textPrimary
                                             : Theme.textMuted
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fontSizeBase
                                    }

                                    Rectangle {
                                        anchors.left:   parent.left
                                        anchors.right:  parent.right
                                        anchors.bottom: parent.bottom
                                        height: 2
                                        color: active ? Theme.accent : "transparent"
                                    }

                                    MouseArea {
                                        id: tabMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        onClicked: panelSettings.detailTabIndex = index
                                    }
                                }
                            }
                        }
                    }

                    StackLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        currentIndex: panelSettings.detailTabIndex

                        JobDetailPanel { id: jobDetailInstance }
                        LogPanel { id: logPanelInstance }
                    }
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
    MrDialog {
        id: settingsDialog
        title: qsTr("Settings")
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

    // --- New Job (submission) modal ---
    // visible binds to appBridge.submissionMode, so every entry point
    // (Jobs menu, Job List's New Job button, the empty detail-tab CTA)
    // opens the same dialog, and AppBridge::setCurrentJobId's
    // "picking a job cancels submission" rule closes it for free.
    // Loader re-creates the form each open so a cancelled draft never
    // leaks into the next submission. NoAutoClose for parity with
    // Settings: the form's Submit/Cancel are the only exits, which also
    // keeps the visible binding intact (a self-close would break it).
    MrDialog {
        id: submissionDialog
        title: qsTr("New Job")
        width: 640
        height: Math.min(720, window.height - 80)
        closePolicy: Popup.NoAutoClose
        visible: appBridge.submissionMode

        Loader {
            anchors.fill: parent
            active: submissionDialog.visible
            sourceComponent: submissionFormComponent
        }

        Component {
            id: submissionFormComponent
            SubmissionForm {
                onSubmitted: (jobId) => {
                    appBridge.submissionMode = false
                    appBridge.currentJobId = jobId
                }
                onCancelled: appBridge.submissionMode = false
                // failed: form's error banner shows the reason; the
                // dialog stays open so the user can fix and retry.
            }
        }
    }

    // --- Edit Job modal ---
    // Driven by appBridge.editJobId (set by openJobEditor from the job
    // context menu, cleared by applyJobEdit/closeJobEditor). Loader
    // re-creates the form each open so it always prefills from a fresh
    // manifest fetch.
    MrDialog {
        id: editJobDialog
        readonly property bool resubmit:
            !!appBridge.editSeed && appBridge.editSeed.resubmit === true
        title: appBridge.editJobId.length > 0
               ? (resubmit
                  ? qsTr("Resubmit chunk as new job — %1").arg(appBridge.editJobId)
                  : qsTr("Edit Job — %1").arg(appBridge.editJobId))
               : qsTr("Edit Job")
        width: 640
        height: Math.min(760, window.height - 80)
        closePolicy: Popup.NoAutoClose
        visible: appBridge.editJobId.length > 0

        Loader {
            anchors.fill: parent
            active: editJobDialog.visible
            sourceComponent: editJobFormComponent
        }

        Component {
            id: editJobFormComponent
            SubmissionForm {
                editSeed: appBridge.editSeed
                onCancelled: appBridge.closeJobEditor()
                // Resubmit mode ends with a normal submission — close
                // the editor and jump to the new job.
                onSubmitted: (jobId) => {
                    appBridge.closeJobEditor()
                    appBridge.currentJobId = jobId
                }
            }
        }
    }

    // --- Farm Cleanup dialog ---
    FarmCleanupDialog {
        id: farmCleanupDialog
    }

    // --- Transient toast ---
    // Failure surface for job actions that happen outside a dialog
    // (e.g. the Edit button's manifest fetch). Auto-hides; a new
    // message restarts the timer.
    Rectangle {
        id: toast
        property alias text: toastLabel.text
        property bool isError: true

        function show(message, error) {
            toastLabel.text = message
            isError = error === undefined ? true : error
            toastTimer.restart()
        }

        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 24
        width: Math.min(toastLabel.implicitWidth + 32, parent.width - 80)
        height: toastLabel.implicitHeight + 16
        radius: Theme.radius
        color: Theme.surfaceAlt
        border.color: toast.isError ? Theme.error : Theme.accent
        border.width: Theme.borderWidth
        visible: toastTimer.running
        z: 1000

        Label {
            id: toastLabel
            anchors.centerIn: parent
            width: Math.min(implicitWidth, toast.parent.width - 112)
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            color: Theme.textBright
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeBase
        }

        Timer {
            id: toastTimer
            interval: 6000
        }

        MouseArea {
            anchors.fill: parent
            onClicked: toastTimer.stop()
        }
    }

    Connections {
        target: appBridge
        function onJobActionFailed(reason) { toast.show(reason) }
        function onEditQueued(message) { toast.show(message, false) }
        function onEditApplied(jobId) {
            toast.show(qsTr("Edit applied to %1").arg(jobId), false)
        }
    }
}

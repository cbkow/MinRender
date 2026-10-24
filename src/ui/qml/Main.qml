import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MinRenderUi 1.0

ApplicationWindow {
    id: window

    width: 1440
    height: 900
    // The chromeless window (macOS only, src/ui/chrome/WindowChrome.h): the
    // content extends under a transparent title bar (Qt 6.9+'s
    // ExpandedClientAreaHint + NoTitleBarBackgroundHint), the native
    // traffic lights stay and the title row below is the title bar.
    // Windows keeps its native frame and title bar; the row is a toolbar
    // under it. BEFORE `visible` — the hints must be in place at creation.
    flags: windowChrome.chromeless
         ? (Qt.Window | Qt.ExpandedClientAreaHint | Qt.NoTitleBarBackgroundHint)
         : Qt.Window
    // ApplicationWindow pads its content by the safe area unless told
    // otherwise — the title row goes UNDER the traffic lights (macOS).
    topPadding: 0
    // startMinimized: context property from main_qt.cpp (--minimized flag,
    // set by the launch-at-login paths). The tray icon restores the window.
    visible: !startMinimized
    // Still set: the taskbar / Alt-Tab / Dock label. AppKit's drawn title
    // text is hidden by WindowChrome; the row shows the hostname instead.
    title: qsTr("minRender Monitor")

    // The traffic lights inside the client area (macOS, hidden in
    // fullscreen): the width the title row's left end keeps clear of.
    readonly property int osLeftInset: windowChrome.nativeLights && window.visibility !== Window.FullScreen ? 70 : 0
    // macOS has its native menu bar; Windows gets the app menu button in
    // the title row instead of an in-window menu strip.
    readonly property bool nativeMenuBar: Qt.platform.os === "osx"

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

    // Persisted split sizes + panel visibility across launches. QtCore's
    // Settings is QSettings under the organization name main_qt.cpp sets
    // (the registry on Windows, a plist on macOS) — UI state only, nothing
    // another program needs, so it may live in the Store package's
    // virtualized store.
    Settings {
        id: panelSettings
        category: "panels"

        property real nodePanelWidth: window.width * 0.28
        property real jobListHeight: (window.height - titleBar.height) * 0.33
        // Active tab in the bottom pane: 0 = Job Detail, 1 = Logs.
        property int detailTabIndex: 0

        property bool nodePanelVisible: true
        property bool jobListVisible: true
    }

    // --- The app's actions, shared by the macOS menu bar and the Windows
    // app menu (the hamburger in the title row). ---
    Action { id: settingsAction;    text: qsTr("Settings…");     onTriggered: settingsDialog.open() }
    Action { id: farmCleanupAction; text: qsTr("Farm Cleanup…"); onTriggered: farmCleanupDialog.open() }
    Action { id: exitAction;        text: qsTr("E&xit");         onTriggered: Qt.quit() }
    Action { id: newJobAction;      text: qsTr("New Job…");      onTriggered: appBridge.submissionMode = true }
    // The job list's selection actions. The shortcuts are live only while
    // the job list has keyboard focus, so Cmd/Ctrl-A in a text field or in
    // the chunk / peer lists keeps its own meaning there.
    readonly property bool jobsFocused: jobListInstance.visible && jobListInstance.listFocused
    readonly property int  jobsSelected: jobListInstance.checkedCount()
    Action { id: selectAllJobsAction; text: qsTr("Select All Jobs"); shortcut: StandardKey.SelectAll
             enabled: window.jobsFocused; onTriggered: jobListInstance.selectAll() }
    Action { id: editJobAction;   text: qsTr("Edit Job…")
             enabled: window.jobsSelected === 1 && appBridge.leaderSupportsJobEdit
             onTriggered: jobListInstance.editSelected() }
    Action { id: pauseJobsAction;  text: qsTr("Pause");     enabled: window.jobsSelected > 0; onTriggered: jobListInstance.pauseSelected() }
    Action { id: resumeJobsAction; text: qsTr("Resume");    enabled: window.jobsSelected > 0; onTriggered: jobListInstance.resumeSelected() }
    Action { id: cancelJobsAction; text: qsTr("Cancel…");   enabled: window.jobsSelected > 0; onTriggered: jobListInstance.cancelSelected() }
    Action { id: deleteJobsAction; text: qsTr("Delete…");   enabled: window.jobsSelected > 0; onTriggered: jobListInstance.deleteSelected() }
    Action {
        id: nodePanelAction
        text: qsTr("Node Panel")
        checkable: true
        checked: panelSettings.nodePanelVisible
        onToggled: panelSettings.nodePanelVisible = checked
    }
    Action {
        id: jobListAction
        text: qsTr("Job List")
        checkable: true
        checked: panelSettings.jobListVisible
        onToggled: panelSettings.jobListVisible = checked
    }
    // Version line stays disabled — it's a label, not an action.
    // Qt.application.version is fed by main_qt.cpp's
    // QCoreApplication::setApplicationVersion, which reads the
    // CMake PROJECT_VERSION at compile time.
    Action { id: versionAction; text: qsTr("minRender v%1").arg(Qt.application.version); enabled: false }
    // No manual update check in Store builds (the Microsoft Store owns
    // updates) or in builds with no updater compiled in —
    // appUpdater.available is compile-time constant.
    Action { id: checkUpdatesAction; text: qsTr("Check for &Updates…"); onTriggered: appUpdater.checkForUpdates() }
    Action { id: docsAction; text: qsTr("Docs"); onTriggered: Qt.openUrlExternally("https://minrender.com/") }

    // macOS: the native menu bar, created on completion (an in-window
    // MenuBar on Windows would sit above the title row).
    Component {
        id: macMenuBarComp
        MenuBar {
            Menu {
                title: qsTr("&File")
                MenuItem { action: settingsAction }
                MenuItem { action: farmCleanupAction }
                MenuSeparator {}
                MenuItem { action: exitAction }
            }
            Menu {
                title: qsTr("&Jobs")
                MenuItem { action: newJobAction }
                MenuSeparator {}
                MenuItem { action: selectAllJobsAction }
                MenuItem { action: editJobAction }
                MenuSeparator {}
                MenuItem { action: pauseJobsAction }
                MenuItem { action: resumeJobsAction }
                MenuSeparator {}
                MenuItem { action: cancelJobsAction }
                MenuItem { action: deleteJobsAction }
            }
            Menu {
                title: qsTr("&View")
                MenuItem { action: nodePanelAction }
                MenuItem { action: jobListAction }
            }
            Menu {
                id: aboutMenu
                title: qsTr("&About")
                MenuItem { action: versionAction }
                MenuSeparator {}
                MenuItem { id: checkUpdatesItem; action: checkUpdatesAction }
                MenuItem { action: docsAction }
                Component.onCompleted: {
                    if (!appUpdater.available)
                        aboutMenu.removeItem(checkUpdatesItem)
                }
            }
        }
    }
    Component.onCompleted: {
        if (window.nativeMenuBar)
            window.menuBar = macMenuBarComp.createObject(window)
    }

    // Windows: the app menu behind the title row's hamburger — the same
    // actions, one flat list.
    ThemedMenu {
        id: appMenu
        ThemedMenuItem { action: settingsAction }
        ThemedMenuItem { action: farmCleanupAction }
        ThemedMenuSeparator {}
        ThemedMenuItem { action: newJobAction }
        ThemedMenuItem { action: editJobAction }
        ThemedMenuItem { action: pauseJobsAction }
        ThemedMenuItem { action: resumeJobsAction }
        ThemedMenuItem { action: cancelJobsAction }
        ThemedMenuItem { action: deleteJobsAction }
        ThemedMenuSeparator {}
        ThemedMenuItem { action: nodePanelAction }
        ThemedMenuItem { action: jobListAction }
        ThemedMenuSeparator {}
        ThemedMenuItem { action: versionAction }
        ThemedMenuItem { action: checkUpdatesAction; visible: appUpdater.available; height: visible ? implicitHeight : 0 }
        ThemedMenuItem { action: docsAction }
        ThemedMenuSeparator {}
        ThemedMenuItem { action: exitAction }
    }

    // ── Title row: the window's title bar ─────────────────────────────
    // Left to right: on macOS the inset where the NATIVE traffic lights
    // sit · the app menu button (Windows only) · this node's hostname, the
    // LEADER badge and its state dot · bare drag room · New Job. On macOS
    // every stretch of the row no control claims DRAGS the window and a
    // double-click zooms, through the handle below (the drag starts after a
    // small movement, so clicks stay clicks); the controls sit on
    // DragMouseAreas so they are drag surface too. On Windows the row is a
    // toolbar under the native title bar.
    Rectangle {
        id: titleBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: Theme.toolStripHeight
        color: Theme.toolbar
        // Drag room (macOS): a gap between the traffic lights and our
        // controls, and a reserve in the middle the content can never fill.
        readonly property int buttonGap: 16
        readonly property int dragReserve: 120

        Rectangle {
            anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
            height: Theme.dividerWidth
            color: Theme.divider
        }

        // The move handle (macOS): under everything else in the row, so a
        // press on a control never reaches it. Windows moves by its native
        // title bar.
        DragMouseArea {
            anchors.fill: parent
            enabled: windowChrome.nativeLights
            onDoubleClicked: (mouse) => { if (windowChrome.chromeless) windowChrome.toggleZoom(window) }
        }

        // The traffic lights' room (close · minimize · zoom end at x ≈ 59 on
        // a standard title bar); they hide in fullscreen.
        Item {
            id: lightsInset
            anchors.left: parent.left; anchors.top: parent.top
            height: parent.height
            width: window.osLeftInset + (window.osLeftInset > 0 ? titleBar.buttonGap : Theme.padding)
        }

        Item {
            id: rightPad
            anchors.right: parent.right; anchors.top: parent.top
            height: parent.height
            width: Theme.padding
        }

        Row {
            id: titleLeft
            anchors.left: lightsInset.right
            anchors.top: parent.top
            height: parent.height - Theme.dividerWidth
            spacing: Theme.spacing

            // The app menu (Windows): the hamburger, where the menu bar used
            // to be. macOS has its native menu bar.
            Rectangle {
                id: menuBtn
                visible: !window.nativeMenuBar
                width: visible ? 30 : 0
                height: parent.height
                anchors.verticalCenter: parent.verticalCenter
                radius: Theme.radiusSmall
                readonly property bool menuOpen: appMenu.visible
                color: menuOpen ? Theme.affordanceHover
                     : menuMA.containsMouse ? Theme.surfaceHover : Theme.hoverIdle
                Behavior on color { ColorAnimation { duration: Theme.hoverMs } }
                Icon {
                    anchors.centerIn: parent
                    name: "list"
                    size: Theme.iconSizeToolbar
                    color: menuBtn.menuOpen || menuMA.containsMouse ? Theme.textBright : Theme.textPrimary
                }
                DragMouseArea {
                    id: menuMA
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: { if (appMenu.visible) appMenu.close(); else appMenu.popup(menuBtn, 0, menuBtn.height + 1) }
                }
            }

            // This node: hostname (the window's real title), the LEADER
            // badge, and a dot for its render state. The details stay in
            // the node panel.
            Label {
                anchors.verticalCenter: parent.verticalCenter
                text: appBridge.thisNodeHostname
                color: Theme.textBright
                font.family: Theme.fontFamily
                font.bold: true
                font.pixelSize: Theme.fontSizeBase
                elide: Text.ElideRight
                width: Math.min(implicitWidth, 320)
            }
            StatusBadge {
                anchors.verticalCenter: parent.verticalCenter
                visible: appBridge.thisNodeIsLeader
                text: qsTr("LEADER")
            }
            Item {
                id: stateDot
                width: 16; height: parent.height
                readonly property string stateText:
                    appBridge.thisNodeSuspended ? qsTr("Suspended by the leader")
                  : appBridge.thisNodeActive    ? qsTr("Accepting work")
                                                : qsTr("Stopped — not accepting work")
                Rectangle {
                    anchors.centerIn: parent
                    width: 8; height: 8; radius: 4
                    color: appBridge.thisNodeSuspended ? Theme.error
                         : appBridge.thisNodeActive    ? Theme.success
                                                       : Theme.textMuted
                    Behavior on color { ColorAnimation { duration: Theme.hoverMs } }
                }
                DragMouseArea { id: dotMA; anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.LeftButton }
                FlatToolTip { visible: dotMA.containsMouse; text: stateDot.stateText }
            }
        }

        // New Job: the one action that should be reachable even with the
        // Jobs panel hidden. An island (a Button eats its press), so the
        // row keeps dragReserve px bare to its left.
        FlatButton {
            id: newJobButton
            anchors.right: rightPad.left
            anchors.verticalCenter: parent.verticalCenter
            anchors.verticalCenterOffset: -Theme.dividerWidth / 2
            height: parent.height - 8
            iconName: "plus"
            text: qsTr("New Job")
            variant: "primary"
            onClicked: appBridge.submissionMode = true
        }
    }

    // Outer horizontal split: NodePanel on the left, everything else on the right.
    SplitView {
        id: outerSplit
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: titleBar.bottom
        anchors.bottom: parent.bottom
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
                                        Behavior on color { ColorAnimation { duration: Theme.hoverMs } }
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
        // Fades in and out over the popup duration; visible tracks the
        // fade so a hidden toast never eats clicks.
        opacity: toastTimer.running ? 1 : 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: Theme.popupMs } }
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

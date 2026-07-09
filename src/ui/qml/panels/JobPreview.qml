import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MinRenderUi 1.0

// Latest-frame preview for the selected job. Polls
// appBridge.jobPreviewInfo every 3 s (same cadence as the chunk table)
// and shows the newest rendered frame via the vendored image://thumb
// provider (QCView's async thumbnail pipeline). EXR files get a
// "Layers" contact sheet — a lift of QCView's inspector layer grid —
// to pick which layer/AOV the big preview shows.
Rectangle {
    id: root
    color: Theme.frameBg

    property string jobId: ""

    property var info: ({ found: false })
    property string selectedLayer: ""
    // Session-only per-job layer memory, so flipping between jobs
    // keeps each one's picked AOV.
    property var layerByJob: ({})

    // Pin state lives on the bridge (set by the frame grid / chunk
    // table over in Job Detail). Pinned previews auto-return to
    // "latest" after a minute, or via the Latest button below.
    readonly property bool pinned: appBridge.previewPinStart >= 0

    function refresh() {
        if (!appBridge.previewSupported || jobId.length === 0) {
            info = { found: false }
            return
        }
        info = pinned
            ? appBridge.rangePreviewInfo(jobId, appBridge.previewPinStart,
                                         appBridge.previewPinEnd)
            : appBridge.jobPreviewInfo(jobId)
    }

    onJobIdChanged: {
        selectedLayer = layerByJob[jobId] || ""
        layersSheet.close()
        refresh()
    }
    Component.onCompleted: refresh()

    Connections {
        target: appBridge
        function onPreviewPinChanged() {
            pinTimeout.restart()
            root.refresh()
        }
    }

    Timer {
        interval: 3000
        repeat: true
        running: root.visible && root.jobId.length > 0
        onTriggered: root.refresh()
    }

    // Auto-return to latest a minute after the last pin action.
    Timer {
        id: pinTimeout
        interval: 60000
        onTriggered: appBridge.clearPreviewPin()
    }

    readonly property string thumbSource:
        info.found
        ? "image://thumb/" + encodeURIComponent(info.file)
          + "?layer=" + encodeURIComponent(selectedLayer)
        : ""

    Image {
        id: previewImage
        anchors {
            left: parent.left
            right: parent.right
            top: parent.top
            bottom: footerBar.top
            margins: 4
        }
        fillMode: Image.PreserveAspectFit
        asynchronous: true
        source: root.thumbSource
        sourceSize.width: 960
    }

    // Empty / loading states
    Label {
        anchors.centerIn: previewImage
        visible: !root.info.found
        text: root.jobId.length === 0 ? qsTr("No job selected")
                                      : qsTr("No frames rendered yet")
        color: Theme.textMuted
        font.pixelSize: Theme.fontSizeBase
    }
    BusyIndicator {
        anchors.centerIn: previewImage
        running: previewImage.status === Image.Loading
        width: 28; height: 28
    }

    // --- Footer: frame info + layer picker entry ---
    Rectangle {
        id: footerBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: Theme.toolStripHeight
        color: Theme.surface

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: Theme.dividerWidth
            color: Theme.divider
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.padding
            anchors.rightMargin: Theme.spacingTight
            spacing: 6

            // Amber "pinned" tag so a stale-looking preview is
            // explainable at a glance.
            Label {
                visible: root.pinned
                text: appBridge.previewPinStart === appBridge.previewPinEnd
                      ? qsTr("\u25C9 f%1").arg(appBridge.previewPinStart)
                      : qsTr("\u25C9 %1-%2").arg(appBridge.previewPinStart)
                                             .arg(appBridge.previewPinEnd)
                color: Theme.warn
                font.pixelSize: Theme.fontSizeSmall
            }

            Label {
                Layout.fillWidth: true
                text: {
                    if (!root.info.found) return qsTr("Preview")
                    let s = root.info.file.split(/[/\\]/).pop()
                    if (root.selectedLayer.length > 0)
                        s += "  ·  " + root.selectedLayer
                    return s
                }
                color: Theme.textSecondary
                font.family: Theme.monoFamily
                font.pixelSize: Theme.fontSizeSmall
                elide: Text.ElideMiddle
            }

            FlatButton {
                visible: root.pinned
                iconName: "broadcast"
                text: qsTr("Latest")
                onClicked: appBridge.clearPreviewPin()
            }

            FlatButton {
                visible: root.info.found === true && root.info.isExr === true
                iconName: "stack"
                text: qsTr("Layers")
                onClicked: layersSheet.openFor()
            }
        }
    }

    // --- EXR layer contact sheet (borrowed from QCView's inspector) ---
    Popup {
        id: layersSheet
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: 560
        height: Math.min(620, parent ? parent.height - 80 : 620)
        padding: 12

        background: Rectangle {
            color: Theme.modalBg
            border.color: Theme.borderStrong
            border.width: Theme.borderWidth
        }
        Overlay.modal: Rectangle { color: Theme.scrim }

        property var layers: []
        function openFor() {
            layers = appBridge.exrLayers(root.info.file)
            open()
        }

        contentItem: ColumnLayout {
            spacing: 8

            Label {
                text: qsTr("EXR layers")
                color: Theme.textBright
                font.bold: true
                font.pixelSize: Theme.fontSizeBase
            }

            GridView {
                id: layerGrid
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                cellWidth: Math.floor(width / 2)
                cellHeight: 168
                model: layersSheet.layers
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: MrScrollBar {}

                delegate: Item {
                    required property string modelData

                    readonly property bool active:
                        root.selectedLayer === modelData
                        || (root.selectedLayer.length === 0 && modelData === "default")

                    width: layerGrid.cellWidth
                    height: layerGrid.cellHeight

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 6
                        spacing: 4

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            color: Theme.frameBg
                            border.color: active ? Theme.accent : Theme.border
                            border.width: active ? 2 : 1

                            Image {
                                anchors.fill: parent
                                anchors.margins: 2
                                fillMode: Image.PreserveAspectFit
                                asynchronous: true
                                sourceSize.width: 240
                                source: "image://thumb/"
                                        + encodeURIComponent(root.info.file)
                                        + "?layer=" + encodeURIComponent(modelData)
                            }
                        }

                        Label {
                            Layout.fillWidth: true
                            text: modelData
                            color: active ? Theme.textBright : Theme.textSecondary
                            font.pixelSize: Theme.fontSizeSmall
                            elide: Text.ElideMiddle
                            horizontalAlignment: Text.AlignHCenter
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            root.selectedLayer = modelData
                            const next = Object.assign({}, root.layerByJob)
                            next[root.jobId] = modelData
                            root.layerByJob = next
                            layersSheet.close()
                        }
                    }
                }
            }
        }
    }
}

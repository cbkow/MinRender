import QtQuick
import QtQuick.Controls.Basic
import MinRenderUi 1.0

// Accent-tinted ProgressBar matching the flat token system. Drop-in
// replacement for QtQuick.Controls.ProgressBar — same `value`/`from`/`to`
// API, but uses Theme.accent for the fill and Theme.surfaceHover for
// the track. Squared corners, 4px tall by default.
ProgressBar {
    id: root

    from: 0
    to: 1

    background: Rectangle {
        implicitWidth:  120
        implicitHeight: 10
        color: Theme.surfaceHover
        radius: Theme.radius
    }

    contentItem: Item {
        implicitWidth:  120
        implicitHeight: 10

        Rectangle {
            width: root.visualPosition * parent.width
            height: parent.height
            color: Theme.accent
            radius: Theme.radius
        }
    }
}

import QtQuick
import QtQuick.Templates as T
import MinRenderUi 1.0

// Themed ScrollBar matching the flat ufb-ish visual language. Derived
// from QtQuick.Templates.ScrollBar (not Basic/Fusion) so we don't
// inherit the default opacity-fade states that hide the thumb at rest —
// on Theme.bg (#161616) the default Basic thumb fades to roughly the
// background colour and disappears.
//
// Usage mirrors plain ScrollBar:
//   ListView { ScrollBar.vertical: MrScrollBar {} }
//   ScrollView { ScrollBar.vertical: MrScrollBar { policy: ScrollBar.AlwaysOn } }
T.ScrollBar {
    id: root

    policy: T.ScrollBar.AsNeeded

    // Fixed thickness regardless of orientation. Qt's defaults grow/
    // shrink on hover; locking it keeps adjacent rows from reflowing.
    implicitWidth:  orientation === Qt.Vertical   ? 10 : 0
    implicitHeight: orientation === Qt.Horizontal ? 10 : 0

    // Transparent track — the thumb alone signals scroll position.
    background: Rectangle {
        color: "transparent"
    }

    contentItem: Rectangle {
        implicitWidth:  root.orientation === Qt.Vertical   ? 10 : 0
        implicitHeight: root.orientation === Qt.Horizontal ? 10 : 0
        radius: Theme.radius
        color: root.pressed ? Theme.textPrimary
             : root.hovered ? Theme.textSecondary
                            : Theme.textMuted
        // No opacity transitions — the thumb stays solid whenever the
        // bar is visible. Visibility is governed entirely by `policy`.
    }
}

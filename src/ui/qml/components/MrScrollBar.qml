import QtQuick
import QtQuick.Templates as T
import MinRenderUi 1.0

// Themed ScrollBar matching the flat ufb-ish visual language. Derived
// from QtQuick.Templates.ScrollBar (not Basic/Fusion) so we own the
// visible/hover/opacity behaviour — the stock styles either fade the
// thumb to near-invisible on dark backgrounds or read the colour from
// a QML palette that QApplication::setPalette can't reach.
//
// Usage mirrors plain ScrollBar:
//   ListView { ScrollBar.vertical: MrScrollBar {} }
//   ScrollView { ScrollBar.vertical: MrScrollBar { policy: ScrollBar.AlwaysOn } }
T.ScrollBar {
    id: root

    policy: T.ScrollBar.AsNeeded

    // T.ScrollBar (Templates) defaults hoverEnabled to false on platforms
    // where Qt.styleHints.useHoverEffects is false (notably macOS with a
    // trackpad). Force it on so the contentItem can react to hover.
    hoverEnabled: true

    // Templates ScrollBar doesn't enforce policy visibility — Basic/
    // Fusion do that via opacity transitions. Bind visible directly so
    // AsNeeded actually hides the bar when content fits, and AlwaysOff
    // hides it unconditionally.
    visible: policy === T.ScrollBar.AlwaysOn
          || (policy === T.ScrollBar.AsNeeded && size < 1.0)

    // Fixed thickness regardless of orientation. Locking it keeps
    // adjacent content from reflowing when the bar appears/disappears.
    implicitWidth:  orientation === Qt.Vertical   ? Theme.scrollBarWidth : 0
    implicitHeight: orientation === Qt.Horizontal ? Theme.scrollBarWidth : 0

    // Transparent track — the thumb alone signals scroll position.
    background: Rectangle {
        color: "transparent"
    }

    contentItem: Rectangle {
        implicitWidth:  root.orientation === Qt.Vertical   ? Theme.scrollBarWidth : 0
        implicitHeight: root.orientation === Qt.Horizontal ? Theme.scrollBarWidth : 0
        radius: Theme.radius
        // Hover is tracked on the thumb itself rather than via
        // root.hovered. The control's hover area covers the full track
        // (often transparent), so a HoverHandler on the thumb makes the
        // brightening match what the user actually sees under the
        // cursor.
        color: root.pressed ? Theme.textSecondary
             : thumbHover.hovered ? Theme.textMuted
                                  : Theme.borderStrong

        HoverHandler {
            id: thumbHover
            cursorShape: Qt.ArrowCursor
        }
    }
}

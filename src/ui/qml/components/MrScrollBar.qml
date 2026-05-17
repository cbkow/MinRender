import QtQuick
import QtQuick.Controls.Basic
import MinRenderUi 1.0

// Themed ScrollBar matching the flat ufb-ish visual language. The default
// Basic-style ScrollBar thumb is a near-black gray that disappears on
// Theme.bg (#161616), so we replace the contentItem with a thumb that
// uses the borderStrong/textMuted/textSecondary progression already
// established for hover/pressed elsewhere.
//
// Usage mirrors plain ScrollBar — drop in via the attached property or
// the explicit ScrollBar.vertical: form:
//   ListView { ScrollBar.vertical: MrScrollBar {} }
//   ScrollView { ScrollBar.vertical: MrScrollBar { policy: ScrollBar.AlwaysOn } }
ScrollBar {
    id: root

    // Default to AsNeeded so the bar disappears in lists that already fit.
    // Override per-site for AlwaysOn (e.g. SubmissionForm).
    policy: ScrollBar.AsNeeded

    // Fixed thickness regardless of orientation. Qt's default fluctuates
    // with hover; locking it keeps adjacent rows from reflowing.
    implicitWidth:  orientation === Qt.Vertical   ? 10 : 0
    implicitHeight: orientation === Qt.Horizontal ? 10 : 0

    // Transparent track — the thumb alone signals scroll position. Keeps
    // tight lists from gaining a visual gutter when the bar appears.
    background: Item {}

    contentItem: Rectangle {
        radius: Theme.radius
        color: root.pressed ? Theme.textSecondary
             : root.hovered ? Theme.textMuted
                            : Theme.borderStrong
    }
}

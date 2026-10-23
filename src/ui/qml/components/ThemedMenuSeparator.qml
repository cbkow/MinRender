import QtQuick
import QtQuick.Controls
import MinRenderUi 1.0

// The divider row of a ThemedMenu: a 1-px Theme.divider rule inset to
// the row's text column, drawn from the tokens rather than Fusion.
MenuSeparator {
    padding: 0
    topPadding: 4
    bottomPadding: 4
    contentItem: Rectangle {
        implicitWidth: 200
        implicitHeight: Theme.dividerWidth
        color: Theme.divider
    }
}

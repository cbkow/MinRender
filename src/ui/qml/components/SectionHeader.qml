import QtQuick
import QtQuick.Controls
import MinRenderUi 1.0

// Bold label used above groups of form fields (SettingsPanel,
// SubmissionForm). Centralised so sizing/colour stays consistent
// if designs change.
Label {
    color: Theme.textPrimary
    font.bold: true
    font.pixelSize: Theme.fontSizeBase
}

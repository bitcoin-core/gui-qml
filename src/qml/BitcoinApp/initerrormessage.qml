import QtQuick
import QtQuick.Dialogs

MessageDialog {
    id: messageDialog
    title: "Bitcoin Core TnG"
    text: message
    onAccepted: Qt.quit()
    Component.onCompleted: visible = true
}

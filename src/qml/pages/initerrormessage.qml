import QtQuick 2.12
import QtQuick.Dialogs 1.3

MessageDialog {
    id: messageDialog
    title: "Bitcoin Core TnG"
    icon: StandardIcon.Critical
    text: message
    onAccepted: Qt.quit()
    Component.onCompleted: visible = true
}

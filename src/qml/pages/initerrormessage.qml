import QtQuick 2.12
import QtQuick.Controls 2.12

ApplicationWindow {
    title: "Bitcoin Core TnG"
    visible: true
    minimumWidth: 500
    minimumHeight: 200

    Dialog {
        anchors.centerIn: parent
        title: qsTr("Error")
        contentItem:
            Label {
                text: message
            }
        visible: true
        standardButtons: Dialog.Ok
        onAccepted: Qt.quit()
    }
}

// Copyright (c) 2021-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

import QtQuick
import QtQuick.Controls

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

// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15

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

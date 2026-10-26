// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import "../controls"

// Shown for errors before any other window exists, so it hosts the alert in
// its own window.
ApplicationWindow {
    id: root
    // The engine sets the `message` context property. AlertPopup has its own
    // `message`, which would bind to itself.
    readonly property string errorMessage: message

    visible: true
    title: qsTr("Bitcoin Core App")
    width: 500
    height: Math.max(250, errorAlert.implicitHeight + 80)
    minimumWidth: 500
    minimumHeight: 250
    color: Theme.color.background

    AlertPopup {
        id: errorAlert
        objectName: "initErrorPopup"
        dim: false
        title: qsTr("There was an issue starting up")
        message: root.errorMessage
        messageObjectName: "initErrorMessage"
        messageTextFormat: Text.PlainText

        AlertAction {
            text: qsTr("Close")
            buttonObjectName: "initErrorCloseButton"
        }

        onClosed: Qt.quit()
        Component.onCompleted: open()
    }
}

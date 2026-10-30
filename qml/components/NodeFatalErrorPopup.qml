// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15

import "../controls"

AlertPopup {
    id: root
    objectName: "nodeFatalErrorPopup"

    function syncOpenState() {
        if (nodeModel.startupError.length > 0 && !opened) {
            open()
        }
    }

    Connections {
        target: nodeModel
        function onStartupErrorChanged() {
            root.syncOpenState()
        }
    }

    Component.onCompleted: syncOpenState()

    z: 100
    dismissible: false
    title: qsTr("Fatal node error")
    message: nodeModel.startupError
    messageObjectName: "nodeFatalErrorText"
    messageAlignment: Text.AlignLeft
    tone: AlertPopup.Error
    iconSource: "image://images/error"
    implicitWidth: Math.max(480, horizontalActionsWidth + 40)

    AlertAction {
        text: qsTr("Shut down")
        buttonObjectName: "nodeFatalShutdownButton"
        closesPopup: false
        onTriggered: nodeModel.requestShutdown()
    }
}

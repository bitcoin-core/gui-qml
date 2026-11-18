// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import org.bitcoincore.qt 1.0
import QtQuick.Layouts 1.15

import "../controls"

AlertPopup {
    id: root
    objectName: "nodeFatalErrorPopup"
    property bool showCompleteDiagnostics: false

    function syncOpenState() {
        if (ApplicationModels.nodeModel.startupError.length > 0 && !opened) {
            open()
        }
    }

    Connections {
        target: ApplicationModels.nodeModel
        function onStartupErrorChanged() {
            root.syncOpenState()
        }
    }

    Component.onCompleted: syncOpenState()

    z: 100
    dismissible: false
    title: qsTr("Fatal node error")
    message: root.showCompleteDiagnostics ? "" : ApplicationModels.nodeModel.startupError
    messageObjectName: "nodeFatalErrorText"
    messageAlignment: Text.AlignLeft
    tone: AlertPopup.Error
    iconSource: "image://images/error"
    implicitWidth: Math.max(480, horizontalActionsWidth + 40)
    content: [
        MonospaceOutputView {
            objectName: "nodeFatalCompleteDiagnostics"
            Layout.fillWidth: true
            Layout.preferredHeight: 240
            visible: root.showCompleteDiagnostics
            listModel: ApplicationModels.nodeModel.startupDiagnostics
            autoScrollToBottom: false
        },
        TextButton {
            objectName: "nodeFatalDetailsButton"
            Layout.alignment: Qt.AlignHCenter
            visible: ApplicationModels.nodeModel.startupDiagnostics !== null
            text: root.showCompleteDiagnostics ? qsTr("Show summary") : qsTr("Show complete diagnostics")
            onClicked: root.showCompleteDiagnostics = !root.showCompleteDiagnostics
        }
    ]

    AlertAction {
        text: qsTr("Shut down")
        buttonObjectName: "nodeFatalShutdownButton"
        closesPopup: false
        onTriggered: ApplicationModels.nodeModel.requestShutdown()
    }
}

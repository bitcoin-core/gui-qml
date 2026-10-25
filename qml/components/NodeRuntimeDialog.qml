// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15

import "../controls"

// Shows the node's message boxes. NodeModel owns visibility: every action
// answers through answerRuntimeDialog and the popup follows runtimeDialogVisible.
AlertPopup {
    id: root
    objectName: "nodeRuntimeDialog"

    component RuntimeAction: AlertAction {
        required property int button
        required property string name
        buttonObjectName: "nodeRuntimeDialogButton" + name
        closesPopup: false
        onTriggered: nodeModel.answerRuntimeDialog(button)
    }

    // Cancel-first order. No, Abort and Cancel are Cancel actions, so Escape
    // answers with them.
    readonly property list<AlertAction> runtimeActions: [
        RuntimeAction { button: DialogButtonBox.No; name: "No"; text: qsTr("No"); role: AlertAction.Cancel },
        RuntimeAction { button: DialogButtonBox.Abort; name: "Abort"; text: qsTr("Abort"); role: AlertAction.Cancel },
        RuntimeAction { button: DialogButtonBox.Cancel; name: "Cancel"; text: qsTr("Cancel"); role: AlertAction.Cancel },
        RuntimeAction { button: DialogButtonBox.Close; name: "Close"; text: qsTr("Close"); role: AlertAction.Neutral },
        RuntimeAction { button: DialogButtonBox.Help; name: "Help"; text: qsTr("Help"); role: AlertAction.Neutral },
        RuntimeAction { button: DialogButtonBox.Reset; name: "Reset"; text: qsTr("Reset"); role: AlertAction.Neutral },
        RuntimeAction { button: DialogButtonBox.Discard; name: "Discard"; text: qsTr("Discard"); role: AlertAction.Neutral },
        RuntimeAction { button: DialogButtonBox.Ignore; name: "Ignore"; text: qsTr("Ignore"); role: AlertAction.Neutral },
        RuntimeAction { button: DialogButtonBox.Retry; name: "Retry"; text: qsTr("Retry") },
        RuntimeAction { button: DialogButtonBox.Apply; name: "Apply"; text: qsTr("Apply") },
        RuntimeAction { button: DialogButtonBox.Yes; name: "Yes"; text: qsTr("Yes") },
        RuntimeAction { button: DialogButtonBox.Ok; name: "Ok"; text: qsTr("OK") }
    ]

    // Not gated on opened: an answer given during the enter transition must
    // still close the popup, and open() and close() are no-ops when settled.
    function syncOpenState() {
        if (nodeModel.runtimeDialogVisible) {
            open()
        } else {
            close()
        }
    }

    Connections {
        target: nodeModel
        function onRuntimeDialogChanged() {
            root.syncOpenState()
        }
    }

    Component.onCompleted: syncOpenState()

    title: nodeModel.runtimeDialogTitle
    message: nodeModel.runtimeDialogMessage
    messageObjectName: "nodeRuntimeDialogMessage"
    messageAlignment: Text.AlignLeft
    iconSource: nodeModel.runtimeDialogIcon
    tone: iconSource.toString().indexOf("error") >= 0 ? AlertPopup.Error
        : iconSource.toString().indexOf("alert") >= 0 ? AlertPopup.Warning
        : AlertPopup.Default
    implicitWidth: Math.max(480, horizontalActionsWidth + 40)
    actionList: {
        const buttons = nodeModel.runtimeDialogButtons
        const actions = []
        for (let i = 0; i < runtimeActions.length; ++i) {
            if (buttons & runtimeActions[i].button) actions.push(runtimeActions[i])
        }
        return actions
    }
    // A message with a single button is acknowledged by Escape too.
    escapeAction: actionList.length === 1 ? actionList[0] : null
}

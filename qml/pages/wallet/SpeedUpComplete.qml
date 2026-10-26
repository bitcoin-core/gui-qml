// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15

import "../../controls"

AlertPopup {
    id: root
    objectName: "speedUpCompletePopup"

    property string txid: ""
    signal done()
    signal viewNewTransaction(string txid)

    tone: AlertPopup.Success
    title: qsTr("Transaction updated")
    message: qsTr("Your replacement transaction was broadcast. Confirmation depends on its fee rate.")
    implicitWidth: Math.max(420, horizontalActionsWidth + 40)
    escapeAction: doneAction

    AlertAction {
        text: qsTr("View new transaction")
        role: AlertAction.Neutral
        buttonObjectName: "speedUpCompleteViewTransactionButton"
        closesPopup: false
        onTriggered: root.viewNewTransaction(root.txid)
    }

    AlertAction {
        id: doneAction
        text: qsTr("Done")
        buttonObjectName: "speedUpCompleteDoneButton"
        closesPopup: false
        onTriggered: root.done()
    }
}

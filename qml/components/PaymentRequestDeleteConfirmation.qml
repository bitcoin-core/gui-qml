// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15

import "../controls"

// Asks before deleting a payment request, which can't be undone, and reports
// a failed delete in an alert. Every "Delete payment request" action uses it.
AlertPopup {
    id: root

    property var wallet: null
    property string requestId: ""
    property string pendingRequestId: ""

    signal deleted(string requestId)

    function confirmDelete(requestWallet, id) {
        if (wallet !== requestWallet) pendingRequestId = ""
        wallet = requestWallet
        requestId = id
        open()
    }

    function deleteRequest() {
        const id = requestId
        if (!wallet || !wallet.removeReceiveRequest(id)) {
            showFailure("")
            return false
        }
        pendingRequestId = id
        return true
    }

    function showFailure(error) {
        failurePopup.message = error || qsTr("The payment request could not be deleted. Please try again.")
        failurePopup.open()
    }

    readonly property Connections walletCompletion: Connections {
        target: root.wallet
        function onReceiveOperationFinished(operation, success) {
            if (operation !== "remove" || !root.pendingRequestId) return
            const id = root.pendingRequestId
            root.pendingRequestId = ""
            if (success) root.deleted(id)
            else root.showFailure(root.wallet.receiveOperationError)
        }
    }

    title: qsTr("Delete payment request?")
    message: qsTr("The request is removed from this wallet. This can't be undone. Payments to its address will still arrive in your wallet, but won't be linked to a payment request.")
    messageObjectName: "deletePaymentRequestMessage"

    AlertAction {
        text: qsTr("Cancel")
        role: AlertAction.Cancel
        buttonObjectName: "deletePaymentRequestCancelButton"
    }

    AlertAction {
        text: qsTr("Delete")
        role: AlertAction.Destructive
        buttonObjectName: "deletePaymentRequestConfirmButton"
        onTriggered: root.deleteRequest()
    }

    property AlertPopup failurePopup: AlertPopup {
        objectName: root.objectName.length > 0 ? root.objectName + "Error" : ""
        parent: root.parent
        title: qsTr("Payment request not deleted")
        messageObjectName: "deletePaymentRequestErrorMessage"

        AlertAction {
            text: qsTr("OK")
            buttonObjectName: "deletePaymentRequestErrorCloseButton"
        }
    }
}

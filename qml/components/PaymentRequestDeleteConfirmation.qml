// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15

import org.bitcoincore.qt 1.0

import "../controls"

// Asks before deleting a payment request, which can't be undone, and reports
// a failed delete in an alert. The request's own delete question decides.
AlertPopup {
    id: root

    property WalletReceiveModel receive: null
    readonly property RequestDeleteController controller: deletion

    //! Asks whether to delete saved request \p id.
    function confirmDelete(id) { return deletion.request(id) }

    RequestDeleteController {
        id: deletion
        receive: root.receive
    }

    shown: deletion.asking
    onCloseRequested: deletion.cancel()
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
        // Confirming ends the question, which closes the alert.
        closesPopup: false
        onTriggered: deletion.confirm()
    }

    property AlertPopup failurePopup: AlertPopup {
        objectName: root.objectName.length > 0 ? root.objectName + "Error" : ""
        parent: root.parent
        shown: deletion.failed
        onCloseRequested: deletion.dismiss()
        title: qsTr("Payment request not deleted")
        message: deletion.error.length > 0 ? deletion.error : qsTr("The payment request could not be deleted. Please try again.")
        messageObjectName: "deletePaymentRequestErrorMessage"

        AlertAction {
            text: qsTr("OK")
            buttonObjectName: "deletePaymentRequestErrorCloseButton"
        }
    }
}

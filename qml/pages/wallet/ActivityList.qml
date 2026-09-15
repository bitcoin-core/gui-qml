// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import org.bitcoincore.qt 1.0
import "../../controls"

PageStack {
    id: stackView
    objectName: "activityStack"
    property var wallet: walletController.selectedWallet

    function detailProperties(txid, outputIndex) {
        if (!wallet) return {}
        const transaction = wallet.transactionActivityModel.transactionDetails(txid)
        if (!transaction.txid) return {}
        let action = null
        if (outputIndex !== undefined && outputIndex >= 0) {
            for (let i = 0; i < transaction.actions.length; ++i) {
                if (transaction.actions[i].outputIndex === outputIndex) action = transaction.actions[i]
            }
            if (!action) return {}
        }
        return {
            txid: transaction.txid,
            outputIndex: action ? action.outputIndex : -1,
            label: action ? action.label : transaction.label,
            address: action ? action.address : transaction.address,
            amount: action ? action.amount : transaction.amount,
            date: Qt.formatDateTime(new Date(transaction.timestamp * 1000), "MMM d, yyyy, h:mm AP"),
            depth: transaction.depth,
            status: transaction.status,
            type: action ? action.direction === TransactionActivityModel.ReceiveAction ? Transaction.RecvWithAddress
                : action.direction === TransactionActivityModel.InternalAction ? Transaction.SendToSelf : Transaction.SendToAddress
                : transaction.type,
            canBump: transaction.canBump,
            replacedByTxid: transaction.isInactive ? transaction.replacedByTxid : "",
            paymentRequests: action ? action.paymentRequests : transaction.paymentRequests
        }
    }

    function navigateToTransaction(txid, outputIndex) {
        if (!wallet) return
        let details = detailProperties(txid, outputIndex)
        if (!details.txid) {
            // A send-result link can arrive before the queued wallet update.
            wallet.transactionActivityModel.reload()
            details = detailProperties(txid, outputIndex)
        }
        if (!details.txid) return
        stackView.pop(null)
        const page = stackView.push("ActivityDetails.qml", details)
        page.showTransaction.connect(stackView.navigateToTransaction)
    }

    function refreshOpenDetails() {
        const page = stackView.currentItem
        if (!page || page.objectName !== "activityDetailsPage") return
        const details = detailProperties(page.txid, page.outputIndex)
        if (!details.txid) {
            stackView.pop(null)
            return
        }
        for (const key in details) page[key] = details[key]
    }

    Connections {
        target: walletController
        function onSelectedWalletChanged() { stackView.pop(null) }
        function onClosePaymentRequestDetailRequested() { stackView.pop(null) }
    }
    Connections {
        target: stackView.wallet ? stackView.wallet.transactionActivityModel : null
        function onDataChanged() { stackView.refreshOpenDetails() }
        function onRowsRemoved() { stackView.refreshOpenDetails() }
        function onModelReset() { stackView.refreshOpenDetails() }
    }
    Binding {
        target: stackView.wallet
        property: "displayUnit"
        value: optionsModel.displayUnit
        when: stackView.wallet !== null
    }

    initialItem: ActivityListPage {
        wallet: stackView.wallet
        onTransactionRequested: function(txid) { stackView.navigateToTransaction(txid) }
        onPaymentRequestRequested: function(requestId) {
            if (stackView.wallet && stackView.wallet.loadPaymentRequestDetail(requestId)) {
                stackView.push("PaymentRequestDetail.qml")
            }
        }
    }
}

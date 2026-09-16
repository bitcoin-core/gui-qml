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
        const transaction = wallet.transactionActivityModel.transactionDetails(txid, true)
        if (!transaction.txid) return {}
        return {
            transactionData: transaction,
            wallet: stackView.wallet,
            outputIndex: outputIndex === undefined ? -1 : outputIndex
        }
    }

    function navigateToTransaction(txid, outputIndex) {
        if (!wallet) return
        let details = detailProperties(txid, outputIndex)
        if (!details.transactionData) {
            // A send-result link can arrive before the queued wallet update.
            wallet.transactionActivityModel.reload()
            details = detailProperties(txid, outputIndex)
        }
        if (!details.transactionData) return
        stackView.pop(null)
        const page = stackView.push("TransactionDetail.qml", details)
        page.showTransaction.connect(stackView.navigateToTransaction)
    }

    function refreshOpenDetails() {
        const page = stackView.currentItem
        if (!page || page.objectName !== "activityDetailsPage") return
        const details = detailProperties(page.txid, page.outputIndex)
        if (!details.transactionData) {
            stackView.pop(null)
            return
        }
        for (const key in details) page[key] = details[key]
    }

    onCurrentItemChanged: refreshOpenDetails()

    Connections {
        target: walletController
        function onSelectedWalletChanged() { stackView.pop(null) }
        function onClosePaymentRequestDetailRequested() { stackView.pop(null) }
    }
    Connections {
        target: stackView.wallet ? stackView.wallet.transactionActivityModel : null
        function onTransactionDetailsChanged() { stackView.refreshOpenDetails() }
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

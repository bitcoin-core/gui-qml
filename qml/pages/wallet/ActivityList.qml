// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import org.bitcoincore.qt 1.0
import "../../controls"
import "../../components"

PageStack {
    id: stackView
    objectName: "activityStack"
    property var wallet: null
    property bool walletViewActive: true

    replaceEnter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.motion.fade(Theme.motion.exit); easing.type: Theme.motion.easing.enter }
    }
    replaceExit: Transition {
        NumberAnimation { property: "opacity"; to: 0; duration: Theme.motion.fade(Theme.motion.exit); easing.type: Theme.motion.easing.exit }
    }

    function openPaymentRequest(requestId) {
        stackView.pop(null)
        paymentRequestModal.openRequest(requestId)
    }

    function navigateToTransaction(txid, outputIndex) {
        if (!wallet || !wallet.available || !txid) return
        const details = { transactionId: txid, wallet: stackView.wallet, walletViewActive: Qt.binding(function() { return stackView.walletViewActive }),
            outputIndex: outputIndex === undefined ? -1 : outputIndex }
        let page
        if (stackView.currentItem && stackView.currentItem.objectName === "activityDetailsPage")
            page = stackView.replace(stackView.currentItem, "TransactionDetail.qml", details, StackView.ReplaceTransition)
        else {
            stackView.pop(null)
            page = stackView.push("TransactionDetail.qml", details)
        }
        if (page && page.showTransaction) page.showTransaction.connect(stackView.navigateToTransaction)
    }

    onWalletChanged: { paymentRequestModal.close(); stackView.pop(null) }

    Connections {
        target: ApplicationModels.walletController
        function onClosePaymentRequestDetailRequested(wallet) {
            if (wallet === stackView.wallet) paymentRequestModal.close()
        }
    }
    Binding {
        target: stackView.wallet
        property: "displayUnit"
        value: ApplicationModels.optionsModel.displayUnit
        when: stackView.wallet !== null
    }

    RequestDetailsHost {
        id: paymentRequestModal
        wallet: stackView.wallet
        contextActive: stackView.walletViewActive
    }

    initialItem: ActivityListPage {
        wallet: stackView.wallet
        onTransactionRequested: function(txid) { stackView.navigateToTransaction(txid) }
        onPaymentRequestRequested: function(requestId) {
            paymentRequestModal.openRequest(requestId)
        }
    }
}

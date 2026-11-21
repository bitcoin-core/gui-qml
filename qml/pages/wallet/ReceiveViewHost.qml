// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
import QtQuick 2.15
import org.bitcoincore.qt 1.0

Item {
    id: root
    property WalletQmlModel wallet: null
    property bool walletViewActive: true
    signal paymentRequestCreated()
    signal addressHistoryRequested()
    Loader {
        anchors.fill: parent
        sourceComponent: root.wallet && root.wallet.receive ? ownedReceive : compatibilityReceive
    }
    Component {
        id: ownedReceive
        ReceiveWorkspace {
            wallet: root.wallet
            walletViewActive: root.walletViewActive
            onPaymentRequestCreated: root.paymentRequestCreated()
            onAddressHistoryRequested: root.addressHistoryRequested()
        }
    }
    Component {
        id: compatibilityReceive
        ReceivePage {
            wallet: root.wallet
            onPaymentRequestCreated: root.paymentRequestCreated()
            onAddressHistoryRequested: root.addressHistoryRequested()
        }
    }
}

// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15

import org.bitcoincore.qt 1.0

import "../../controls"
import "../settings" as SettingsPages

// Wallet settings and their sub-pages for one wallet. Shown in the "Wallet
// settings" section of the shared settings page while its wallet is active.
PageStack {
    id: root
    objectName: "walletSettingsStack"

    property WalletQmlModel wallet: null

    signal selectWalletRequested()
    signal receiveRequested()
    signal paymentRequestRequested(string requestId)

    function resetToRoot() {
        if (root.depth > 1) root.pop(null, StackView.Immediate)
    }

    function openAddressHistory() {
        if (!root.wallet) return
        root.wallet.addressListModel.refresh()
        root.resetToRoot()
        root.push(addressListPage, {}, StackView.Immediate)
    }

    function openPassword(updating) {
        if (!root.wallet) return
        root.resetToRoot()
        root.push(walletPasswordPage, { "updating": updating }, StackView.Immediate)
    }

    function openSignVerifyMessage(initialTab) {
        if (!root.wallet) return
        root.resetToRoot()
        const properties = initialTab === undefined || initialTab === null ? {} : { "initialTab": initialTab }
        root.push(signVerifyPage, properties, StackView.Immediate)
    }

    function startBackup() {
        if (!root.wallet) return
        root.resetToRoot()
        if (root.currentItem && typeof root.currentItem.startBackup === "function") {
            root.currentItem.startBackup()
        }
    }

    initialItem: SettingsPages.WalletSectionPage {
        wallet: root.wallet
        onSelectWalletRequested: root.selectWalletRequested()
        onPasswordRequested: root.push(walletPasswordPage, {
            "updating": root.wallet ? root.wallet.isEncrypted : false
        })
        onSignVerifyMessageRequested: root.push(signVerifyPage)
        onCoinsRequested: root.push(coinsPage)
        onAddressesRequested: {
            if (!root.wallet) return
            root.wallet.addressListModel.refresh()
            root.push(addressListPage)
        }
    }

    Component {
        id: coinsPage
        WalletCoins {
            wallet: root.wallet
            onBack: root.pop()
        }
    }

    Component {
        id: walletPasswordPage
        WalletPasswordSettings {
            wallet: root.wallet
            onBack: root.pop()
            onSaved: root.pop()
        }
    }

    Component {
        id: signVerifyPage
        SignVerifyMessage {
            wallet: root.wallet
            onBack: root.pop()
        }
    }

    Component {
        id: addressListPage
        AddressList {
            wallet: root.wallet
            onBack: root.pop()
            // Keep the Addresses page on the stack: leaving for the request
            // view and returning to settings should land back here, not on
            // the settings root.
            onReceiveRequested: root.receiveRequested()
            onPaymentRequestRequested: function(requestId) { root.paymentRequestRequested(requestId) }
        }
    }
}

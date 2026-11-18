// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import org.bitcoincore.qt 1.0

import "../../controls"
import "../settings" as SettingsPages

WalletSettingsPort {
    id: root
    onResetRequested: if (ownWalletSettings.count > 0) (ownWalletSettings.itemAt(0) as WalletSettingsStack).resetToRoot()

    SettingsPages.WalletSectionPage {
        anchors.fill: parent
        visible: ownWalletSettings.count === 0
        onSelectWalletRequested: root.selectWalletRequested()
    }

    // One stack per wallet: selecting another wallet destroys this one with
    // its pages and their controllers, and creates a new stack.
    Repeater {
        id: ownWalletSettings
        model: root.wallet ? [root.wallet] : []
        delegate: WalletSettingsStack {
            required property var modelData
            anchors.fill: parent
            wallet: modelData as WalletQmlModel
            onSelectWalletRequested: root.selectWalletRequested()
        }
    }
}

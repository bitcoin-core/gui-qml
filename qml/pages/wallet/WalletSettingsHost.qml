// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import org.bitcoincore.qt 1.0

import "../../controls"
import "../settings" as SettingsPages

WalletSettingsPort {
    id: root
    onResetRequested: if (ownWalletSettings.item) (ownWalletSettings.item as WalletSettingsStack).resetToRoot()

    SettingsPages.WalletSectionPage {
        anchors.fill: parent
        visible: !ownWalletSettings.active
        onSelectWalletRequested: root.selectWalletRequested()
    }

    Loader {
        id: ownWalletSettings
        anchors.fill: parent
        active: root.wallet !== null
        source: active ? "WalletSettingsStack.qml" : ""
        Binding {
            target: ownWalletSettings.item
            property: "wallet"
            value: root.wallet as WalletQmlModel
            when: ownWalletSettings.status === Loader.Ready
        }
        Connections {
            target: ownWalletSettings.item
            function onWalletChanged() { (ownWalletSettings.item as WalletSettingsStack).resetToRoot() }
            function onSelectWalletRequested() { root.selectWalletRequested() }
        }
    }
}

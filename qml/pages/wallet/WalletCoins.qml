// Copyright (c) 2025-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0
import "../../controls"
import "../../components"
Page {
    id: root
    objectName: "walletCoinsPage"
    property WalletQmlModel wallet: walletController.selectedWallet
    signal back()
    background: null
    header: SettingsHeader {
        //: Wallet settings page listing its individual unspent coins.
        title: qsTr("View coins"); backButtonObjectName: "walletCoinsBackButton"
        onBack: root.back()
    }
    CoinsList {
        anchors.fill: parent; anchors.margins: parent.width < 600 ? 16 : 28
        wallet: root.wallet; selectionMode: false
        Component.onCompleted: if (coins) coins.update()
    }
}

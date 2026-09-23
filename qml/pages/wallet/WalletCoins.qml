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
    readonly property real contentHorizontalPadding: width >= 900 ? 56 : width >= 640 ? 40 : 24
    readonly property real maximumContentWidth: 840
    signal back()
    background: null
    header: SettingsHeader {
        //: Wallet settings page listing its individual unspent coins.
        title: qsTr("View coins"); backButtonObjectName: "walletCoinsBackButton"
        onBack: root.back()
    }
    CoinsList {
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.topMargin: parent.width < 600 ? 16 : 28
        anchors.bottomMargin: parent.width < 600 ? 16 : 28
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.max(0, Math.min(parent.width - root.contentHorizontalPadding * 2,
                                    root.maximumContentWidth))
        wallet: root.wallet; selectionMode: false
        Component.onCompleted: if (coins) coins.update()
    }
}

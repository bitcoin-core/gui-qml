// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import "../../controls"
import "../../components"

Page {
    id: root
    objectName: "settingsWallet"

    signal back
    property bool showBackButton: true
    readonly property int maximumContentWidth: 450

    background: null
    contentWidth: root.maximumContentWidth

    header: SettingsHeader {
        title: qsTr("External signer")
        showBackButton: root.showBackButton
        backButtonObjectName: "settingsWalletBack"
        onBack: root.back()
    }

    ScrollView {
        anchors.fill: parent
        contentWidth: width
        clip: true

        ColumnLayout {
            width: Math.min(parent.width, root.maximumContentWidth)
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 0

            SettingsRestartNotice {
                objectName: "walletRestartNotice"
                visible: optionsModel.walletSettingsDirty
                Layout.fillWidth: true
                Layout.topMargin: 10
                Layout.bottomMargin: 20
            }

            WalletSettings {
                Layout.fillWidth: true
            }
        }
    }
}

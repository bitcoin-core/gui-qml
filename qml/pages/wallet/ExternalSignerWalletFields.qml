// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0
import "../../controls"

ColumnLayout {
    id: fields
    required property string defaultWalletName
    required property string nameError
    required property bool creatingWallet
    signal nameEdited()
    property alias walletName: nameEntry.text
    spacing: 20

    CoreText {
        Layout.fillWidth: true
        text: qsTr("This wallet stores public descriptors on this computer and uses your external signer for addresses and signing.")
        color: Theme.color.neutral7
        textStyle: Theme.text.body
        wrap: true
        horizontalAlignment: Text.AlignLeft
    }

    LabeledTextField {
        id: nameEntry
        objectName: "externalWalletNameEntry"
        Layout.fillWidth: true
        label: qsTr("Wallet name")
        fieldObjectName: "externalWalletNameInput"
        placeholderText: qsTr("e.g. Hardware wallet")
        text: fields.defaultWalletName
        supportingText: qsTr("You cannot change this later.")
        fieldSurface: true
        fieldBackgroundColor: Theme.color.surface2
        enabled: !fields.creatingWallet
        onTextEdited: {
            fields.nameEdited()
            ApplicationModels.walletController.clearWalletLoadStatus()
        }
    }

    CoreText {
        objectName: "externalWalletNameError"
        Layout.fillWidth: true
        visible: text.length > 0
        text: fields.nameError.length > 0 ? fields.nameError : ApplicationModels.walletController.walletLoadError
        color: Theme.color.dangerText
        textStyle: Theme.text.caption
        wrap: true
        horizontalAlignment: Text.AlignLeft
    }
}

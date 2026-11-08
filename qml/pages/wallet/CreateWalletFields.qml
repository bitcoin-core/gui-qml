// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0
import "../../controls"

ColumnLayout {
    id: fields
    required property bool watchOnly
    required property string nameError
    required property bool validXpub
    signal nameEdited()
    signal submitRequested()
    property alias walletName: nameEntry.text
    property alias xpubText: xpubEntry.text
    property alias encryptWallet: encryptCheck.checked
    property alias passwordText: passwordEntry.text
    property alias confirmText: confirmEntry.text
    readonly property bool passwordLossAcknowledged: acknowledgement.checked
    readonly property bool unencryptedWalletAcknowledged: unencryptedAcknowledgement.checked

    function clearSecrets() {
        passwordEntry.text = ""
        confirmEntry.text = ""
    }
    function openUnencryptedWarning() {
        unencryptedWarning.open()
    }

    spacing: 16

    LabeledTextField {
        id: nameEntry
        objectName: "createWalletNameEntry"
        Layout.fillWidth: true
        label: qsTr("Wallet name")
        fieldObjectName: "createWalletNameInput"
        placeholderText: qsTr("e.g. My bitcoin wallet")
        supportingText: qsTr("You cannot change this later.")
        fieldSurface: true
        labelSpacing: 6
        messageSpacing: 8
        fieldBackgroundColor: Theme.color.surface2
        onTextEdited: {
            fields.nameEdited()
            ApplicationModels.walletController.clearWalletCreateStatus()
            ApplicationModels.walletController.clearWalletLoadStatus()
        }
    }

    // Consent in a form is a checkbox; nothing applies until Create.
    FormSection {
        objectName: "createWalletSecuritySection"
        visible: !fields.watchOnly
        Layout.fillWidth: true
        Layout.topMargin: 8
        showBackground: false
        title: qsTr("Security")
        rowSpacing: 16

        CheckBox {
            id: encryptCheck
            objectName: "createWalletEncryptCheckBox"
            Layout.fillWidth: true
            text: qsTr("Encrypt this wallet with a strong password.")
            description: qsTr("Recommended")
            checked: true
            onCheckedChanged: {
                if (!checked) acknowledgement.checked = false
                if (checked) unencryptedAcknowledgement.checked = false
            }
        }

        PasswordTextField {
            id: passwordEntry
            objectName: "createWalletPasswordEntry"
            visible: encryptCheck.checked
            Layout.fillWidth: true
            label: qsTr("Password")
            fieldObjectName: "createWalletPasswordInput"
            placeholderText: qsTr("Enter password…")
            fieldSurface: true
            labelSpacing: 6
            messageSpacing: 8
            fieldBackgroundColor: Theme.color.surface2
            onTextEdited: ApplicationModels.walletController.clearWalletCreateStatus()
        }
        PasswordTextField {
            id: confirmEntry
            objectName: "createWalletConfirmPasswordEntry"
            visible: encryptCheck.checked
            Layout.fillWidth: true
            label: qsTr("Confirm password")
            fieldObjectName: "createWalletPasswordRepeatInput"
            placeholderText: qsTr("Enter password again…")
            fieldSurface: true
            labelSpacing: 6
            messageSpacing: 8
            fieldBackgroundColor: Theme.color.surface2
            errorText: text.length > 0 && passwordEntry.text !== text
                ? qsTr("Passwords don't match") : ""
            onTextEdited: ApplicationModels.walletController.clearWalletCreateStatus()
        }

        CheckBox {
            id: acknowledgement
            objectName: "createWalletPasswordConfirmToggle"
            visible: encryptCheck.checked
            Layout.fillWidth: true
            text: qsTr("I understand that if I lose or forget this password I might lose access to the bitcoin stored in this wallet.")
        }

        CheckBox {
            id: unencryptedAcknowledgement
            objectName: "createWalletUnencryptedConfirmToggle"
            visible: !encryptCheck.checked
            Layout.fillWidth: true
            text: qsTr("I understand this wallet will be unencrypted. Anyone with access to its file may be able to spend my bitcoin. I cannot set a password later.")
        }
    }

    LabeledTextArea {
        id: xpubEntry
        objectName: "createWalletXpubEntry"
        visible: fields.watchOnly
        Layout.fillWidth: true
        label: qsTr("Extended public key (xpub)")
        inputObjectName: "watchOnlyXpubInput"
        placeholderText: qsTr("Enter your key…")
        supportingText: qsTr("Only public information is imported. Private keys stay with your signer.")
        fieldSurface: true
        labelSpacing: 6
        messageSpacing: 8
        fieldBackgroundColor: Theme.color.surface2
        errorText: text.trim().length > 0 && !fields.validXpub
            ? qsTr("Enter a valid extended public key (xpub)") : ""
        onTextEdited: ApplicationModels.walletController.clearWalletLoadStatus()
    }

    CoreText {
        objectName: "createWalletFormErrorText"
        visible: text.length > 0
        Layout.fillWidth: true
        text: fields.nameError.length > 0 ? fields.nameError
            : fields.watchOnly ? ApplicationModels.walletController.walletLoadError : ApplicationModels.walletController.walletCreateError
        color: Theme.color.dangerText
        textStyle: Theme.text.caption
        wrapMode: Text.WordWrap
        horizontalAlignment: Text.AlignLeft
    }

    AlertPopup {
        id: unencryptedWarning
        objectName: "createWalletUnencryptedWarning"
        parent: Overlay.overlay
        title: qsTr("Create without encryption?")
        message: qsTr("This wallet file will be unencrypted. Anyone with access to it may be able to spend your bitcoin. You won't be able to set a password later.")
        AlertAction {
            text: qsTr("Cancel")
            role: AlertAction.Cancel
            buttonObjectName: "createWalletWarningCancelButton"
        }
        AlertAction {
            text: qsTr("Create wallet")
            role: AlertAction.Normal
            buttonObjectName: "createWalletWarningConfirmButton"
            onTriggered: fields.submitRequested()
        }
    }
}

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
    required property bool xpubChecked
    required property bool validXpub
    signal nameEdited()
    signal nameIntent(string value)
    signal keyIntent(string value)
    signal encryptionIntent(bool value)
    property WalletOperationModel operation: null
    property string submissionError: ""
    signal submitRequested()
    property bool showOperationErrorDetails: false
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
        placeholderText: qsTr("Eg. My bitcoin wallet...")
        supportingText: qsTr("You cannot change this later.")
        fieldSurface: true
        labelSpacing: 6
        messageSpacing: 8
        fieldBackgroundColor: Theme.color.surface2
        onTextEdited: {
            fields.nameEdited()
            fields.nameIntent(nameEntry.text)
        }
    }

    Separator {
        Layout.fillWidth: true
        Layout.topMargin: Theme.space.xs
        Layout.bottomMargin: Theme.space.xs
    }

    CoreText {
        visible: !fields.watchOnly
        Layout.fillWidth: true
        text: qsTr("SECURITY")
        color: Theme.color.neutral6
        textStyle: Theme.text.captionStrong
        horizontalAlignment: Text.AlignLeft
    }

    CheckBox {
        id: encryptCheck
        objectName: "createWalletEncryptCheckBox"
        visible: !fields.watchOnly
        Layout.fillWidth: true
        text: qsTr("Encrypt this wallet with a strong password.")
        description: qsTr("Recommended")
        checked: true
        onCheckedChanged: {
            fields.encryptionIntent(checked)
            if (!checked) acknowledgement.checked = false
            if (checked) unencryptedAcknowledgement.checked = false
        }
    }

    PasswordTextField {
        id: passwordEntry
        objectName: "createWalletPasswordEntry"
        visible: !fields.watchOnly && encryptCheck.checked
        Layout.fillWidth: true
        label: qsTr("Password")
        fieldObjectName: "createWalletPasswordInput"
        placeholderText: qsTr("Enter password…")
        fieldSurface: true
        labelSpacing: 6
        messageSpacing: 8
        fieldBackgroundColor: Theme.color.surface2

    }
    PasswordTextField {
        id: confirmEntry
        objectName: "createWalletConfirmPasswordEntry"
        visible: !fields.watchOnly && encryptCheck.checked
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

    }

    // Consent in a form is a checkbox.
    CheckBox {
        id: acknowledgement
        objectName: "createWalletPasswordConfirmToggle"
        visible: !fields.watchOnly && encryptCheck.checked
        Layout.fillWidth: true
        text: qsTr("I understand that if I lose or forget this password I might lose access to the bitcoin stored in this wallet.")
    }

    CheckBox {
        id: unencryptedAcknowledgement
        objectName: "createWalletUnencryptedConfirmToggle"
        visible: !fields.watchOnly && !encryptCheck.checked
        Layout.fillWidth: true
        text: qsTr("I understand this wallet will be unencrypted. Anyone with access to its file may be able to spend my bitcoin. I cannot set a password later.")
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
        errorText: text.trim().length > 0 && fields.xpubChecked && !fields.validXpub
            ? qsTr("Enter a valid extended public key (xpub)") : ""
        onTextEdited: fields.keyIntent(xpubEntry.text)
    }

    CoreText {
        objectName: "createWalletFormErrorText"
        visible: text.length > 0
        Layout.fillWidth: true
        text: fields.nameError.length > 0 ? fields.nameError
            : fields.submissionError.length > 0 ? fields.submissionError : fields.operation ? fields.operation.error.message : ""
        color: Theme.color.dangerText
        textStyle: Theme.text.caption
        wrapMode: Text.WordWrap
        horizontalAlignment: Text.AlignLeft
    }

    WalletOperationWarnings {
        Layout.fillWidth: true
        operation: fields.operation
        summaryObjectName: "createWalletFormWarningText"
    }

    TextButton {
        objectName: "walletOperationErrorDetailsButton"
        visible: fields.nameError.length === 0 && operationErrorDetails.count > 0
        text: fields.showOperationErrorDetails ? qsTr("Hide error details") : qsTr("Show error details")
        onClicked: fields.showOperationErrorDetails = !fields.showOperationErrorDetails
    }

    ListView {
        id: operationErrorDetails
        objectName: "walletOperationErrorDetails"
        Layout.fillWidth: true
        Layout.preferredHeight: visible ? 120 : 0
        visible: fields.showOperationErrorDetails && fields.nameError.length === 0 && count > 0
        model: fields.operation ? fields.operation.errorDetails : null
        clip: true
        reuseItems: true
        cacheBuffer: 0
        delegate: CoreText {
            required property string content
            width: operationErrorDetails.width
            text: content
            textFormat: Text.PlainText
            textStyle: Theme.text.caption
            wrapMode: Text.WordWrap
        }
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

// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0
import "../../controls"
import "../../components"

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
    readonly property bool passwordLossAcknowledged: acknowledgement.loadedTrailingItem
        ? (acknowledgement.loadedTrailingItem as OptionSwitch).checked : false
    readonly property bool unencryptedWalletAcknowledged: unencryptedAcknowledgement.loadedTrailingItem
        ? (unencryptedAcknowledgement.loadedTrailingItem as OptionSwitch).checked : false

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
        fieldBackgroundColor: Theme.color.neutral2
        onTextEdited: {
            fields.nameEdited()
            ApplicationModels.walletController.clearWalletCreateStatus()
            ApplicationModels.walletController.clearWalletLoadStatus()
        }
    }

    Separator {
        Layout.fillWidth: true
        Layout.topMargin: 4
        Layout.bottomMargin: 4
    }

    CoreText {
        visible: !fields.watchOnly
        Layout.fillWidth: true
        text: qsTr("SECURITY")
        color: Theme.color.neutral6
        font: Theme.text.captionStrong.font
        horizontalAlignment: Text.AlignLeft
    }

    CheckBox {
        id: encryptCheck
        objectName: "createWalletEncryptCheckBox"
        visible: !fields.watchOnly
        Layout.fillWidth: true
        text: qsTr("Encrypt this wallet with a strong password.")
        checked: true
        onCheckedChanged: {
            if (!checked && acknowledgement.loadedTrailingItem)
                (acknowledgement.loadedTrailingItem as OptionSwitch).checked = false
            if (checked && unencryptedAcknowledgement.loadedTrailingItem)
                (unencryptedAcknowledgement.loadedTrailingItem as OptionSwitch).checked = false
        }
    }
    CoreText {
        visible: !fields.watchOnly
        Layout.fillWidth: true
        Layout.leftMargin: 30
        Layout.topMargin: -12
        text: qsTr("Recommended")
        color: Theme.color.neutral7
        font: Theme.text.caption.font
        horizontalAlignment: Text.AlignLeft
    }

    PasswordTextField {
        id: passwordEntry
        objectName: "createWalletPasswordEntry"
        visible: !fields.watchOnly && encryptCheck.checked
        Layout.fillWidth: true
        label: qsTr("Password")
        fieldObjectName: "createWalletPasswordInput"
        placeholderText: qsTr("Enter password...")
        fieldSurface: true
        labelSpacing: 6
        messageSpacing: 8
        fieldBackgroundColor: Theme.color.neutral2
        onTextEdited: ApplicationModels.walletController.clearWalletCreateStatus()
    }
    PasswordTextField {
        id: confirmEntry
        objectName: "createWalletConfirmPasswordEntry"
        visible: !fields.watchOnly && encryptCheck.checked
        Layout.fillWidth: true
        label: qsTr("Confirm password")
        fieldObjectName: "createWalletPasswordRepeatInput"
        placeholderText: qsTr("Enter password again...")
        fieldSurface: true
        labelSpacing: 6
        messageSpacing: 8
        fieldBackgroundColor: Theme.color.neutral2
        errorText: text.length > 0 && passwordEntry.text !== text
            ? qsTr("Passwords don't match") : ""
        onTextEdited: ApplicationModels.walletController.clearWalletCreateStatus()
    }

    ListRow {
        id: acknowledgement
        objectName: "createWalletPasswordConfirmToggle"
        visible: !fields.watchOnly && encryptCheck.checked
        Layout.fillWidth: true
        description: qsTr("I understand that if I lose or forget this password I might lose access to the bitcoin stored in this wallet.")
        showDivider: false
        accessibleRole: Accessible.CheckBox
        trailingItem: OptionSwitch { }
        onClicked: (loadedTrailingItem as OptionSwitch).toggle()
    }

    ListRow {
        id: unencryptedAcknowledgement
        objectName: "createWalletUnencryptedConfirmToggle"
        visible: !fields.watchOnly && !encryptCheck.checked
        Layout.fillWidth: true
        description: qsTr("I understand this wallet will be unencrypted. Anyone with access to its file may be able to spend my bitcoin. I cannot set a password later.")
        showDivider: false
        accessibleRole: Accessible.CheckBox
        trailingItem: OptionSwitch { }
        onClicked: (loadedTrailingItem as OptionSwitch).toggle()
    }

    LabeledTextArea {
        id: xpubEntry
        objectName: "createWalletXpubEntry"
        visible: fields.watchOnly
        Layout.fillWidth: true
        label: qsTr("Extended public key (xpub)")
        inputObjectName: "watchOnlyXpubInput"
        placeholderText: qsTr("Enter your key...")
        supportingText: qsTr("Only public information is imported. Private keys stay with your signer.")
        fieldSurface: true
        labelSpacing: 6
        messageSpacing: 8
        fieldBackgroundColor: Theme.color.neutral2
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
        color: Theme.color.red
        font: Theme.text.caption.font
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

// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import "../../controls"
import "../../components"

OnboardingView {
    id: root
    objectName: "createWalletFormPage"
    showNavigationBar: false
    usesSharedNavigation: true
    property bool watchOnly: false
    property bool modalView: false
    property bool creating: false
    property string nameError: ""
    readonly property bool validXpub: loadedChildView
        && loadedChildView.xpubText.trim().length >= 100
        && walletController.validateXpub(loadedChildView.xpubText.trim())
    readonly property bool passwordsMatch: loadedChildView
        && loadedChildView.passwordText.length > 0
        && loadedChildView.passwordText === loadedChildView.confirmText
    readonly property bool canCreate: loadedChildView && walletController.initialized && !creating
        && !walletController.walletLoadInProgress && loadedChildView.walletName.trim().length > 0
        && (watchOnly ? validXpub : loadedChildView.encryptWallet
            ? passwordsMatch && loadedChildView.passwordLossAcknowledged
            : loadedChildView.unencryptedWalletAcknowledged)

    signal cancel()
    signal createRequested(string name, string xpub, string passphrase)

    function submit() {
        if (!canCreate) return
        const availabilityError = walletController.walletNameAvailabilityError(loadedChildView.walletName)
        if (availabilityError.length > 0) {
            nameError = availabilityError
            return
        }
        createRequested(loadedChildView.walletName.trim(), loadedChildView.xpubText.trim(),
            watchOnly || !loadedChildView.encryptWallet ? "" : loadedChildView.passwordText)
    }

    function clearSecrets() {
        if (loadedChildView) loadedChildView.clearSecrets()
    }

    title: ""
    heading: watchOnly ? qsTr("View-only wallet") : qsTr("Single-key wallet")
    subheading: watchOnly
        ? qsTr("See balances and activity without keeping spending keys in this app. An external signer is needed to spend.")
        : qsTr("You can control it in this application.\nWallet data will be stored locally in your hard drive.")
    maximumContentWidth: 800
    showBackButton: !creating && navigationStack
        ? (navigationStack.canGoBack !== undefined
            ? navigationStack.canGoBack : navigationStack.depth > 1) : false
    showCloseButton: modalView && !creating
    primaryButtonText: creating ? qsTr("Creating…") : qsTr("Create wallet")
    primaryButtonObjectName: "createWalletFormCreateButton"
    primaryButtonEnabled: canCreate
    onCloseClicked: root.cancel()
    onPrimaryClicked: {
        if (!root.canCreate) return
        if (root.watchOnly || root.loadedChildView.encryptWallet) root.submit()
        else root.loadedChildView.openUnencryptedWarning()
    }

    Component.onCompleted: {
        walletController.clearWalletCreateStatus()
        walletController.clearWalletLoadStatus()
    }
    Component.onDestruction: clearSecrets()

    childView: ColumnLayout {
        id: fields
        property alias walletName: nameEntry.text
        property alias xpubText: xpubEntry.text
        property alias encryptWallet: encryptCheck.checked
        property alias passwordText: passwordEntry.text
        property alias confirmText: confirmEntry.text
        readonly property bool passwordLossAcknowledged: acknowledgement.loadedTrailingItem
            ? acknowledgement.loadedTrailingItem.checked : false
        readonly property bool unencryptedWalletAcknowledged: unencryptedAcknowledgement.loadedTrailingItem
            ? unencryptedAcknowledgement.loadedTrailingItem.checked : false

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
                root.nameError = ""
                walletController.clearWalletCreateStatus()
                walletController.clearWalletLoadStatus()
            }
        }

        Separator {
            Layout.fillWidth: true
            Layout.topMargin: 4
            Layout.bottomMargin: 4
        }

        CoreText {
            visible: !root.watchOnly
            Layout.fillWidth: true
            text: qsTr("SECURITY")
            color: Theme.color.neutral6
            font: Theme.text.captionStrong.font
            horizontalAlignment: Text.AlignLeft
        }

        CheckBox {
            id: encryptCheck
            objectName: "createWalletEncryptCheckBox"
            visible: !root.watchOnly
            Layout.fillWidth: true
            text: qsTr("Encrypt this wallet with a strong password.")
            checked: true
            onCheckedChanged: {
                if (!checked && acknowledgement.loadedTrailingItem)
                    acknowledgement.loadedTrailingItem.checked = false
                if (checked && unencryptedAcknowledgement.loadedTrailingItem)
                    unencryptedAcknowledgement.loadedTrailingItem.checked = false
            }
        }
        CoreText {
            visible: !root.watchOnly
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
            visible: !root.watchOnly && encryptCheck.checked
            Layout.fillWidth: true
            label: qsTr("Password")
            fieldObjectName: "createWalletPasswordInput"
            placeholderText: qsTr("Enter password...")
            fieldSurface: true
            labelSpacing: 6
            messageSpacing: 8
            fieldBackgroundColor: Theme.color.neutral2
            onTextEdited: walletController.clearWalletCreateStatus()
        }
        PasswordTextField {
            id: confirmEntry
            objectName: "createWalletConfirmPasswordEntry"
            visible: !root.watchOnly && encryptCheck.checked
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
            onTextEdited: walletController.clearWalletCreateStatus()
        }

        ListRow {
            id: acknowledgement
            objectName: "createWalletPasswordConfirmToggle"
            visible: !root.watchOnly && encryptCheck.checked
            Layout.fillWidth: true
            description: qsTr("I understand that if I lose or forget this password I might lose access to the bitcoin stored in this wallet.")
            showDivider: false
            accessibleRole: Accessible.CheckBox
            trailingItem: OptionSwitch { }
            onClicked: loadedTrailingItem.toggle()
        }

        ListRow {
            id: unencryptedAcknowledgement
            objectName: "createWalletUnencryptedConfirmToggle"
            visible: !root.watchOnly && !encryptCheck.checked
            Layout.fillWidth: true
            description: qsTr("I understand this wallet will be unencrypted. Anyone with access to its file may be able to spend my bitcoin. I cannot set a password later.")
            showDivider: false
            accessibleRole: Accessible.CheckBox
            trailingItem: OptionSwitch { }
            onClicked: loadedTrailingItem.toggle()
        }

        LabeledTextArea {
            id: xpubEntry
            objectName: "createWalletXpubEntry"
            visible: root.watchOnly
            Layout.fillWidth: true
            label: qsTr("Extended public key (xpub)")
            inputObjectName: "watchOnlyXpubInput"
            placeholderText: qsTr("Enter your key...")
            supportingText: qsTr("Only public information is imported. Private keys stay with your signer.")
            fieldSurface: true
            labelSpacing: 6
            messageSpacing: 8
            fieldBackgroundColor: Theme.color.neutral2
            errorText: text.trim().length > 0 && !root.validXpub
                ? qsTr("Enter a valid extended public key (xpub)") : ""
            onTextEdited: walletController.clearWalletLoadStatus()
        }

        CoreText {
            objectName: "createWalletFormErrorText"
            visible: text.length > 0
            Layout.fillWidth: true
            text: root.nameError.length > 0 ? root.nameError
                : root.watchOnly ? walletController.walletLoadError : walletController.walletCreateError
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
                onTriggered: root.submit()
            }
        }
    }
}

// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import org.bitcoincore.qt 1.0

import "../../components"

OnboardingView {
    id: root
    readonly property CreateWalletFields fieldsView: root.loadedChildView as CreateWalletFields
    objectName: "createWalletFormPage"
    showNavigationBar: false
    usesSharedNavigation: true
    property bool watchOnly: false
    property bool modalView: false
    property bool creating: false
    readonly property WalletCreationModel editor: WalletCreationModel {
        creation: ApplicationModels.walletController ? ApplicationModels.walletController.creation : null
    }
    property WalletOperationModel operation: editor ? editor.operation : null
    readonly property string nameError: editor ? editor.name.error : qsTr("Wallet creation is unavailable.")
    readonly property bool validXpub: editor && !editor.pendingValidation && editor.extendedPublicKey.error.length === 0
        && editor.extendedPublicKey.text.length > 0
    readonly property bool passwordsMatch: root.fieldsView
        && root.fieldsView.passwordText.length > 0
        && root.fieldsView.passwordText === root.fieldsView.confirmText
    readonly property bool canCreate: root.fieldsView && editor && editor.canCreate && !creating
        && (watchOnly ? validXpub : root.fieldsView.encryptWallet
            ? passwordsMatch && root.fieldsView.passwordLossAcknowledged
            : root.fieldsView.unencryptedWalletAcknowledged)

    signal cancel()
    signal operationSubmitted(WalletOperationModel operation)

    function submit() {
        if (!canCreate) return
        const accepted = editor.create(watchOnly || !root.fieldsView.encryptWallet ? "" : root.fieldsView.passwordText,
            watchOnly || !root.fieldsView.encryptWallet ? "" : root.fieldsView.confirmText)
        clearSecrets()
        if (accepted) operationSubmitted(accepted)
    }

    function clearSecrets() {
        if (root.fieldsView) root.fieldsView.clearSecrets()
    }

    title: ""
    heading: watchOnly ? qsTr("Watch-only wallet") : qsTr("Single-key wallet")
    subheading: watchOnly
        ? qsTr("See balances and activity without keeping spending keys in this app. An external signer is needed to spend.")
        : qsTr("You can control it in this application.\nWallet data will be stored locally in your hard drive.")
    maximumContentWidth: 800
    showBackButton: !creating && navigationStack
        ? (navigationStack.canGoBack !== undefined
            ? navigationStack.canGoBack : navigationStack.depth > 1) : false
    showCloseButton: modalView && !creating
    primaryButtonText: creating ? qsTr("Creating…") : editor && editor.pendingValidation ? qsTr("Checking…") : qsTr("Create wallet")
    primaryButtonObjectName: "createWalletFormCreateButton"
    primaryButtonEnabled: canCreate
    onCloseClicked: root.cancel()
    onPrimaryClicked: {
        if (!root.canCreate) return
        if (root.watchOnly || root.fieldsView.encryptWallet) root.submit()
        else root.fieldsView.openUnencryptedWarning()
    }

    Component.onCompleted: if (editor) editor.chooseKind(watchOnly ? WalletCreationModel.WatchOnly : WalletCreationModel.Software)
    Component.onDestruction: { clearSecrets(); if (editor) editor.discard() }

    childView: CreateWalletFields {
        watchOnly: root.watchOnly
        nameError: root.nameError
        validXpub: root.validXpub
        xpubChecked: root.editor !== null && !root.editor.pendingValidation
        operation: root.operation
        submissionError: root.editor ? root.editor.error.message : ""
        onNameIntent: function(value) { if (root.editor) root.editor.name.edit(value) }
        onKeyIntent: function(value) { if (root.editor) root.editor.extendedPublicKey.edit(value) }
        onEncryptionIntent: function(value) { if (root.editor) root.editor.setEncryptRequested(value) }
        onSubmitRequested: root.submit()
    }
}

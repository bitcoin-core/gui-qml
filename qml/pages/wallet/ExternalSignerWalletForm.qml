// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import org.bitcoincore.qt 1.0
import "../../components"

OnboardingView {
    id: root
    readonly property ExternalSignerWalletFields fieldsView: root.loadedChildView as ExternalSignerWalletFields
    objectName: "externalSignerWalletFormPage"
    showNavigationBar: false
    usesSharedNavigation: true

    property bool modalView: false
    property string defaultWalletName: ""
    property WalletCreationModel editor: ApplicationModels.walletController.newCreation()
    property WalletOperationModel operation: editor ? editor.operation : null
    readonly property string nameError: editor ? editor.name.error : qsTr("Wallet creation is unavailable.")
    readonly property bool creatingWallet: operation && operation.busy
    readonly property bool canCreate: editor && editor.canCreate && !creatingWallet

    signal cancel()
    signal operationSubmitted(WalletOperationModel operation)

    function submit() {
        if (!canCreate) return
        const accepted = editor.create("", "")
        if (accepted) operationSubmitted(accepted)
    }

    title: ""
    heading: qsTr("External signer wallet")
    subheading: ApplicationModels.walletController.externalSignerError.length > 0
        ? ApplicationModels.walletController.externalSignerError
        : ApplicationModels.walletController.externalSignerName.length > 0
          ? qsTr("Connected signer: %1").arg(ApplicationModels.walletController.externalSignerName)
          : qsTr("Connect one external signer to continue.")
    maximumContentWidth: 800
    showBackButton: !creatingWallet && navigationStack && navigationStack.depth > 1
    showCloseButton: modalView && !creatingWallet
    primaryButtonText: creatingWallet ? qsTr("Creating…") : editor && editor.pendingValidation ? qsTr("Checking…") : qsTr("Create wallet")
    primaryButtonObjectName: "createExternalWalletButton"
    primaryButtonEnabled: canCreate
    onCloseClicked: root.cancel()
    onPrimaryClicked: root.submit()

    Component.onCompleted: {
        if (editor) {
            editor.chooseKind(WalletCreationModel.ExternalSigner)
            editor.name.edit(defaultWalletName)
        }
        ApplicationModels.walletController.refreshExternalSignerStatus()
    }
    Component.onDestruction: if (editor) editor.discard()

    childView: ExternalSignerWalletFields {
        defaultWalletName: root.defaultWalletName
        nameError: root.nameError
        creatingWallet: root.creatingWallet
        operation: root.operation
        submissionError: root.editor ? root.editor.error.message : ""
        onNameIntent: function(value) { if (root.editor) root.editor.name.edit(value) }
    }

}

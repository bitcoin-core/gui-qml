// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import org.bitcoincore.qt 1.0
import "../../controls"

OnboardingView {
    id: root
    readonly property ExternalSignerWalletFields fieldsView: root.loadedChildView as ExternalSignerWalletFields
    objectName: "externalSignerWalletFormPage"
    showNavigationBar: false
    usesSharedNavigation: true

    property bool modalView: false
    property string defaultWalletName: ""
    property string nameError: ""
    property bool submitted: false
    readonly property bool creatingWallet: submitted || ApplicationModels.walletController.walletLoadInProgress
    readonly property bool canCreate: ApplicationModels.walletController.initialized
        && ApplicationModels.walletController.canCreateExternalSignerWallet
        && !creatingWallet && root.fieldsView
        && root.fieldsView.walletName.trim().length > 0

    signal cancel()
    signal created(var wallet)

    function submit() {
        if (!canCreate) return
        nameError = ApplicationModels.walletController.walletNameAvailabilityError(root.fieldsView.walletName)
        if (nameError.length > 0) return
        submitted = true
        if (!ApplicationModels.walletController.createExternalSignerWallet(root.fieldsView.walletName.trim()))
            submitted = false
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
    primaryButtonText: creatingWallet ? qsTr("Creating…") : qsTr("Create wallet")
    primaryButtonObjectName: "createExternalWalletButton"
    primaryButtonEnabled: canCreate
    onCloseClicked: root.cancel()
    onPrimaryClicked: root.submit()

    Component.onCompleted: {
        ApplicationModels.walletController.clearWalletLoadStatus()
        ApplicationModels.walletController.refreshExternalSignerStatus()
    }

    Connections {
        target: ApplicationModels.walletController
        function onWalletLoadSucceeded(wallet) {
            if (!root.submitted) return
            root.submitted = false
            root.created(wallet)
        }
        function onWalletLoadErrorChanged() {
            if (root.submitted && ApplicationModels.walletController.walletLoadError.length > 0)
                root.submitted = false
        }
    }

    childView: ExternalSignerWalletFields {
        defaultWalletName: root.defaultWalletName
        nameError: root.nameError
        creatingWallet: root.creatingWallet
        onNameEdited: root.nameError = ""
    }

}

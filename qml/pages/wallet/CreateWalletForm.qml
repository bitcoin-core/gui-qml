// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import org.bitcoincore.qt 1.0

import "../../controls"

OnboardingView {
    id: root
    readonly property CreateWalletFields fieldsView: root.loadedChildView as CreateWalletFields
    objectName: "createWalletFormPage"
    showNavigationBar: false
    usesSharedNavigation: true
    property bool watchOnly: false
    property bool modalView: false
    property bool creating: false
    property string nameError: ""
    readonly property bool validXpub: root.fieldsView
        && root.fieldsView.xpubText.trim().length >= 100
        && ApplicationModels.walletController.validateXpub(root.fieldsView.xpubText.trim())
    readonly property bool passwordsMatch: root.fieldsView
        && root.fieldsView.passwordText.length > 0
        && root.fieldsView.passwordText === root.fieldsView.confirmText
    readonly property bool canCreate: root.fieldsView && ApplicationModels.walletController.initialized && !creating
        && !ApplicationModels.walletController.walletLoadInProgress && root.fieldsView.walletName.trim().length > 0
        && (watchOnly ? validXpub : root.fieldsView.encryptWallet
            ? passwordsMatch && root.fieldsView.passwordLossAcknowledged
            : root.fieldsView.unencryptedWalletAcknowledged)

    signal cancel()
    signal createRequested(string name, string xpub, string passphrase)

    function submit() {
        if (!canCreate) return
        const availabilityError = ApplicationModels.walletController.walletNameAvailabilityError(root.fieldsView.walletName)
        if (availabilityError.length > 0) {
            nameError = availabilityError
            return
        }
        createRequested(root.fieldsView.walletName.trim(), root.fieldsView.xpubText.trim(),
            watchOnly || !root.fieldsView.encryptWallet ? "" : root.fieldsView.passwordText)
    }

    function clearSecrets() {
        if (root.fieldsView) root.fieldsView.clearSecrets()
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
        if (root.watchOnly || root.fieldsView.encryptWallet) root.submit()
        else root.fieldsView.openUnencryptedWarning()
    }

    Component.onCompleted: {
        ApplicationModels.walletController.clearWalletCreateStatus()
        ApplicationModels.walletController.clearWalletLoadStatus()
    }
    Component.onDestruction: clearSecrets()

    childView: CreateWalletFields {
        watchOnly: root.watchOnly
        nameError: root.nameError
        validXpub: root.validXpub
        onNameEdited: root.nameError = ""
        onSubmitRequested: root.submit()
    }

}

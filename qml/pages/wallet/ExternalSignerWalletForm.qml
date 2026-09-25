// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15
import "../../controls"
import "../../components"

OnboardingView {
    id: root
    objectName: "externalSignerWalletFormPage"
    showNavigationBar: false
    usesSharedNavigation: true

    property bool modalView: false
    property string defaultWalletName: ""
    property string nameError: ""
    property bool submitted: false
    readonly property bool creatingWallet: submitted || walletController.walletLoadInProgress
    readonly property bool canCreate: walletController.initialized
        && walletController.canCreateExternalSignerWallet
        && !creatingWallet && loadedChildView
        && loadedChildView.walletName.trim().length > 0

    signal cancel()
    signal created()

    function submit() {
        if (!canCreate) return
        nameError = walletController.walletNameAvailabilityError(loadedChildView.walletName)
        if (nameError.length > 0) return
        submitted = true
        if (!walletController.createExternalSignerWallet(loadedChildView.walletName.trim()))
            submitted = false
    }

    title: ""
    heading: qsTr("External signer wallet")
    subheading: walletController.externalSignerError.length > 0
        ? walletController.externalSignerError
        : walletController.externalSignerName.length > 0
          ? qsTr("Connected signer: %1").arg(walletController.externalSignerName)
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
        walletController.clearWalletLoadStatus()
        walletController.refreshExternalSignerStatus()
    }

    Connections {
        target: walletController
        function onWalletLoadSucceeded() {
            if (!root.submitted) return
            root.submitted = false
            root.created()
        }
        function onWalletLoadErrorChanged() {
            if (root.submitted && walletController.walletLoadError.length > 0)
                root.submitted = false
        }
    }

    childView: ColumnLayout {
        id: fields
        property alias walletName: nameEntry.text
        spacing: 20

        CoreText {
            Layout.fillWidth: true
            text: qsTr("This wallet stores public descriptors on this computer and uses your external signer for addresses and signing.")
            color: Theme.color.neutral7
            font: Theme.text.body.font
            wrap: true
            horizontalAlignment: Text.AlignLeft
        }

        LabeledTextField {
            id: nameEntry
            objectName: "externalWalletNameEntry"
            Layout.fillWidth: true
            label: qsTr("Wallet name")
            fieldObjectName: "externalWalletNameInput"
            placeholderText: qsTr("Eg. Hardware wallet...")
            text: root.defaultWalletName
            supportingText: qsTr("You cannot change this later.")
            fieldSurface: true
            fieldBackgroundColor: Theme.color.neutral2
            enabled: !root.creatingWallet
            onTextEdited: {
                root.nameError = ""
                walletController.clearWalletLoadStatus()
            }
        }

        CoreText {
            objectName: "externalWalletNameError"
            Layout.fillWidth: true
            visible: text.length > 0
            text: root.nameError.length > 0 ? root.nameError : walletController.walletLoadError
            color: Theme.color.red
            font: Theme.text.caption.font
            wrap: true
            horizontalAlignment: Text.AlignLeft
        }
    }
}

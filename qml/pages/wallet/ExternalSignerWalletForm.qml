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
    property bool nameEdited: false
    property bool submitted: false
    // Whether the form offers to go back to the setup step, which it does
    // when that step was skipped on the way here.
    property bool canChangeSigner: false
    readonly property bool checking: externalSignerModel.checking
    readonly property bool creatingWallet: submitted || walletController.walletLoadInProgress
    readonly property bool canCreate: walletController.initialized
        && externalSignerModel.deviceReady
        && !creatingWallet && !checking && loadedChildView
        && loadedChildView.walletName.trim().length > 0

    signal cancel()
    signal created(var wallet)
    signal changeSignerRequested()

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
    subheading: externalSignerModel.error.length > 0
        ? externalSignerModel.error
        : externalSignerModel.deviceName.length > 0
          ? qsTr("Connected signer: %1").arg(externalSignerModel.deviceName)
          //: Shown on external signer wallet creation while HWI responds but no device is found.
          : qsTr("Connect and unlock your device, then check again.")
    maximumContentWidth: 800
    showBackButton: !creatingWallet && navigationStack && navigationStack.depth > 1
    showCloseButton: modalView && !creatingWallet
    primaryButtonText: creatingWallet ? qsTr("Creating…") : qsTr("Create wallet")
    primaryButtonObjectName: "createExternalWalletButton"
    primaryButtonEnabled: canCreate
    // Only needed until a usable device is found.
    secondaryButtonText: externalSignerModel.deviceReady ? ""
        //: Check again button while the hardware wallet is being looked for.
        : checking ? qsTr("Checking…")
        //: Looks for the connected hardware wallet again during external signer wallet creation.
        : qsTr("Check again")
    secondaryButtonObjectName: "externalWalletCheckAgainButton"
    secondaryButtonEnabled: !creatingWallet && !checking
    onCloseClicked: root.cancel()
    onPrimaryClicked: root.submit()
    onSecondaryClicked: externalSignerModel.check()

    // The flow checked the signer just before showing this form.
    Component.onCompleted: walletController.clearWalletLoadStatus()

    // A device found by checking again names the wallet, unless the name
    // was already typed.
    Connections {
        target: externalSignerModel
        function onCheckFinished() {
            if (root.nameEdited || root.creatingWallet || !root.loadedChildView) return
            root.loadedChildView.walletName = externalSignerModel.suggestedWalletName
        }
    }

    Connections {
        target: walletController
        function onWalletLoadSucceeded(wallet) {
            if (!root.submitted) return
            root.submitted = false
            root.created(wallet)
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
            textStyle: Theme.text.body
            wrap: true
            horizontalAlignment: Text.AlignLeft
        }

        // A working signer skips the setup step on the way here; this goes
        // back to it to pick another executable. Coming from setup, Back
        // does that already.
        LinkButton {
            objectName: "externalWalletChangeSignerButton"
            Layout.alignment: Qt.AlignLeft
            visible: root.canChangeSigner
            //: Opens the external signer setup step from wallet naming, to choose another signer executable.
            text: qsTr("Change signer executable")
            enabled: !root.creatingWallet && !root.checking
            onClicked: root.changeSignerRequested()
        }

        LabeledTextField {
            id: nameEntry
            objectName: "externalWalletNameEntry"
            Layout.fillWidth: true
            label: qsTr("Wallet name")
            fieldObjectName: "externalWalletNameInput"
            placeholderText: qsTr("e.g. Hardware wallet")
            text: root.defaultWalletName
            supportingText: qsTr("You cannot change this later.")
            fieldSurface: true
            fieldBackgroundColor: Theme.color.surface2
            enabled: !root.creatingWallet
            onTextEdited: {
                root.nameEdited = true
                root.nameError = ""
                walletController.clearWalletLoadStatus()
            }
        }

        CoreText {
            objectName: "externalWalletNameError"
            Layout.fillWidth: true
            visible: text.length > 0
            text: root.nameError.length > 0 ? root.nameError : walletController.walletLoadError
            color: Theme.color.dangerText
            textStyle: Theme.text.caption
            wrap: true
            horizontalAlignment: Text.AlignLeft
        }
    }
}

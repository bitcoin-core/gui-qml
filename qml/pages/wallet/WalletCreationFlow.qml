// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import org.bitcoincore.qt 1.0
import "../../controls"
import "../../components"

Item {
    id: root
    objectName: "createWalletWizard"
    clip: true
    property bool modalView: false
    property bool onboardingEntry: false
    property bool waitForWalletDiscovery: false
    property WalletOperationModel operation: null
    readonly property bool creatingWallet: operation !== null && operation.busy
    readonly property bool importingWallet: operation !== null && operation.kind === WalletOperationModel.Restore && !operation.terminal
    readonly property bool completedWatchOnly: operation !== null && operation.creationKind === WalletOperationModel.WatchOnly
    readonly property bool completedEncrypted: operation !== null && operation.encrypted
    property bool readyPending: false
    // The wallet just created or imported. The completion pages act on it,
    // even if another wallet is selected meanwhile.
    readonly property WalletModel completedWallet: operation !== null ? operation.resultWallet : null
    readonly property alias currentItem: pages.currentItem
    readonly property OnboardingView currentOnboardingPage: pages.currentItem as OnboardingView
    readonly property alias depth: pages.depth
    readonly property alias busy: pages.busy
    readonly property alias pageStack: pages
    readonly property alias navigationBar: navigationBar
    readonly property bool walletDiscoveryReady: ApplicationModels.walletController.initialized
        && (!waitForWalletDiscovery || ApplicationModels.walletListModel.walletDirLoaded)
    signal finished(bool openActivity, WalletModel wallet)

    function showingCompletion() {
        const page = root.currentItem ? root.currentItem.objectName : ""
        return page === "walletCreationReadyPage" || page === "importWalletSuccessPage"
            || page === "externalWalletCreatedPage"
    }

    function completedWalletIsOpen() {
        return root.completedWallet !== null && root.completedWallet.available !== false
            && ApplicationModels.walletController.rowForWallet(root.completedWallet) >= 0
    }

    // A completion page for a wallet that has since closed has nothing
    // left to go to, so it closes too.
    function closeIfCompletedWalletClosed() {
        if (root.showingCompletion() && !root.completedWalletIsOpen()) root.finished(false, null)
    }

    function push(item, properties, operation) {
        if (operation !== undefined) return pages.push(item, properties, operation)
        if (properties !== undefined) return pages.push(item, properties)
        return pages.push(item)
    }
    function pop() { return pages.pop() }
    function goBack() { pages.goBack() }

    function showOperationResult() {
        if (root.busy) return
        if (!root.operation || !root.operation.terminal || root.showingCompletion()) return
        if (root.operation.phase === WalletOperationModel.Partial || root.operation.phase === WalletOperationModel.Uncertain) {
            if (!root.currentItem || root.currentItem.objectName !== "walletCreationOutcomePage") root.push(outcomePage, { "operation": root.operation }, StackView.Immediate)
            return
        }
        if (root.operation.kind === WalletOperationModel.Restore) {
            if (root.operation.phase === WalletOperationModel.Succeeded) root.push(importSuccess, { "operation": root.operation }, StackView.Immediate)
            else if (root.operation.phase === WalletOperationModel.Failed || root.operation.phase === WalletOperationModel.Rejected) {
                if (!root.currentItem || root.currentItem.objectName !== "walletImportErrorPage") root.push(importError, { "operation": root.operation }, StackView.Immediate)
            }
            return
        }
        if (root.operation.phase !== WalletOperationModel.Succeeded) return
        if (root.operation.creationKind === WalletOperationModel.ExternalSigner) root.push(externalConfirm, { "operation": root.operation }, StackView.Immediate)
        else root.showReady()
    }

    Connections {
        target: root.operation
        function onChanged() { root.showOperationResult() }
    }

    function showReady() {
        if (root.busy) {
            root.readyPending = true
            return
        }
        root.readyPending = false
        root.push(readyPage, { "wallet": root.completedWallet,
                               "watchOnly": root.completedWatchOnly,
                               "encryptedWallet": root.completedEncrypted,
                               "modalView": root.modalView, "operation": root.operation }, StackView.Immediate)
    }

    function chooseImportFile() {
        if (root.importingWallet) return
        if (automationPathField.text.length > 0) {
            const path = automationPathField.text
            automationPathField.text = ""
            root.importFile(path)
            return
        }
        importFileDialog.open()
    }

    function importFile(path) {
        if (!path || root.importingWallet || root.showingCompletion()) return
        root.operation = ApplicationModels.walletController.creation.restoreLocalFile(path)
        importPresentations.offer(root.operation)
        root.showOperationResult()
    }

    WalletOperationPresentationModel { id: importPresentations; showTerminalResults: false }
    Repeater {
        model: importPresentations
        delegate: WalletOperationDialog {
            presenter: importPresentations
            showResult: false
            maximumWidth: Math.min(420, root.width - 40)
        }
    }

    Component.onCompleted: {
        if (!ApplicationModels.walletController.initialized) ApplicationModels.nodeModel.startNodeInitializionThread()
        ApplicationModels.walletController.creation.refreshSigners()
        root.showOperationResult()
    }

    AppFileDialog {
        id: importFileDialog
        objectName: "walletImportFileDialog"
        nameFilters: [qsTr("Wallet backup files (*.bak *.dat)"), qsTr("All files (*)")]
        onAccepted: {
            if (selectedFile.toString().length > 0 && !root.importingWallet) {
                root.operation = ApplicationModels.walletController.creation.restore(selectedFile)
                importPresentations.offer(root.operation)
                root.showOperationResult()
            }
        }
    }

    // The native file picker is not available to the QML functional test bridge.
    TextField {
        id: automationPathField
        objectName: "importWalletPathField"
        visible: false
    }

    Connections {
        target: root.completedWallet
        ignoreUnknownSignals: true
        function onAvailableChanged() {
            root.closeIfCompletedWalletClosed()
        }
    }

    Connections {
        target: ApplicationModels.walletController
        function onOpenWalletsChanged() {
            root.closeIfCompletedWalletClosed()
        }

    }

    OnboardingNavigationBar {
        id: navigationBar
        objectName: "walletCreationNavigationBar"
        width: parent.width
        surfaceLevel: root.modalView ? 1 : 0
        buttonSidePadding: SizeClass.isCompact(width) ? 16 : 24
        visible: root.currentOnboardingPage !== null && root.currentOnboardingPage.usesSharedNavigation
        height: visible ? implicitHeight : 0
        title: visible ? root.currentOnboardingPage.title : ""
        showBackButton: visible && root.currentOnboardingPage.showBackButton
        showCloseButton: visible && root.currentOnboardingPage.showCloseButton
        onBackClicked: root.goBack()
        onCloseClicked: {
            if (root.currentOnboardingPage) root.currentOnboardingPage.closeClicked()
        }
    }

    PageStack {
        id: pages
        objectName: "walletCreationPageStack"
        anchors.top: navigationBar.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        clip: true
        initialItem: root.modalView ? typePage : startPage
        onBusyChanged: {
            if (!busy && root.readyPending) root.showReady()
            if (!busy) root.showOperationResult()
        }
    }

    Component {
        id: startPage
        OnboardingView {
            objectName: "walletCreationStartPage"
            showNavigationBar: false
            usesSharedNavigation: true
            title: ""
            maximumContentWidth: 640
            heading: qsTr("Your wallet starts here")
            subheading: qsTr("Create a wallet to manage your keys in this app, or import an existing wallet file.")
            showCloseButton: !root.importingWallet
            primaryButtonText: qsTr("Create wallet")
            secondaryButtonText: qsTr("Import wallet")
            primaryButtonObjectName: "createWalletButton"
            secondaryButtonObjectName: "importWalletButton"
            primaryButtonEnabled: root.walletDiscoveryReady && !root.importingWallet
            secondaryButtonEnabled: root.walletDiscoveryReady && !root.importingWallet
            onCloseClicked: root.finished(false, null)
            onPrimaryClicked: root.push(typePage)
            onSecondaryClicked: root.chooseImportFile()

            childView: Item {
                implicitHeight: root.walletDiscoveryReady ? 0 : 48
                SpinningIndicator {
                    objectName: "createWalletDiscoveryBusyIndicator"
                    size: 32
                    color: Theme.color.neutral7
                    anchors.centerIn: parent
                    visible: !root.walletDiscoveryReady
                    running: visible
                }
            }
        }
    }

    Component {
        id: typePage
        WalletCreationTypePage {
            modalView: root.modalView
            onboardingEntry: root.onboardingEntry
            importingWallet: root.importingWallet
            onCancel: root.finished(false, null)
            onRegularSelected: root.push(formPage, { "watchOnly": false,
                                                       "modalView": root.modalView })
            onWatchOnlySelected: root.push(formPage, { "watchOnly": true,
                                                         "modalView": root.modalView })
            onExternalSignerSelected: {
                ApplicationModels.walletController.clearWalletLoadStatus()
                ApplicationModels.walletController.creation.refreshSigners()
                root.push(externalWallet, {
                    "defaultWalletName": ApplicationModels.signers ? ApplicationModels.signers.suggestedWalletName : "external_signer"
                })
            }
            onImportSelected: root.chooseImportFile()
        }
    }

    Component {
        id: formPage
        CreateWalletForm {
            creating: root.creatingWallet || root.readyPending
            onCancel: root.finished(false, null)
            onOperationSubmitted: function(accepted) {
                root.operation = accepted
                root.showOperationResult()
            }
        }
    }

    Component {
        id: outcomePage
        WalletCreationOutcomePage { onDone: root.finished(false, null); onOpenRequested: function(wallet) { root.finished(true, wallet) } }
    }

    Component {
        id: readyPage
        WalletCreationReadyPage {
            onDone: root.finished(root.operation ? root.operation.canOpenResult : root.completedWalletIsOpen(), root.completedWallet)
        }
    }

    Component {
        id: importError
        WalletImportErrorPage {
            onCancel: root.finished(false, null)
            onRetryRequested: root.chooseImportFile()
        }
    }
    Component {
        id: importSuccess
        WalletImportCompletePage {
            id: originalImportResult
            onDone: root.finished(originalImportResult.operation.canOpenResult, originalImportResult.operation.resultWallet)
        }
    }
    Component {
        id: externalWallet
        ExternalSignerWalletForm {
            modalView: root.modalView
            onCancel: root.finished(false, null)
            onOperationSubmitted: function(accepted) {
                root.operation = accepted
                root.showOperationResult()
            }
        }
    }
    Component {
        id: externalConfirm
        WalletCreationExternalReadyPage {
            id: originalExternalResult
            onDone: root.finished(originalExternalResult.operation.canOpenResult, originalExternalResult.operation.resultWallet)
        }
    }
}

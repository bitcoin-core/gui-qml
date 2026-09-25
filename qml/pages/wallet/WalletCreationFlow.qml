// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../../controls"

Item {
    id: root
    objectName: "createWalletWizard"
    clip: true
    property bool modalView: false
    property bool waitForWalletDiscovery: false
    property bool creatingWallet: false
    property bool completedWatchOnly: false
    property bool completedEncrypted: false
    property bool readyPending: false
    readonly property alias currentItem: pages.currentItem
    readonly property alias depth: pages.depth
    readonly property alias busy: pages.busy
    readonly property alias pageStack: pages
    readonly property alias navigationBar: navigationBar
    readonly property bool walletDiscoveryReady: walletController.initialized
        && (!waitForWalletDiscovery || walletListModel.walletDirLoaded)
    signal finished(bool openActivity)

    function push(item, properties, operation) {
        if (operation !== undefined) return pages.push(item, properties, operation)
        if (properties !== undefined) return pages.push(item, properties)
        return pages.push(item)
    }
    function pop() { return pages.pop() }
    function goBack() { pages.goBack() }

    function showReady() {
        if (root.busy) {
            root.readyPending = true
            return
        }
        root.readyPending = false
        root.push(readyPage, { "watchOnly": root.completedWatchOnly,
                               "encryptedWallet": root.completedEncrypted,
                               "modalView": root.modalView }, StackView.Immediate)
    }

    Component.onCompleted: {
        if (!walletController.initialized) nodeModel.startNodeInitializionThread()
        walletController.refreshExternalSignerStatus()
    }

    Connections {
        target: walletController
        function onWalletCreateSucceeded() {
            if (!root.creatingWallet) return
            root.creatingWallet = false
            if (root.currentItem && root.currentItem.clearSecrets)
                root.currentItem.clearSecrets()
            root.showReady()
        }
        function onWalletCreateErrorChanged() {
            if (root.creatingWallet && walletController.walletCreateError.length > 0)
                root.creatingWallet = false
        }
        function onWalletLoadErrorChanged() {
            if (root.creatingWallet && walletController.walletLoadError.length > 0)
                root.creatingWallet = false
        }
    }

    OnboardingNavigationBar {
        id: navigationBar
        objectName: "walletCreationNavigationBar"
        width: parent.width
        isOnSurface: root.modalView
        buttonSidePadding: width >= 640 ? 24 : 16
        closeButtonSize: CloseButton.Large
        visible: root.currentItem && root.currentItem.usesSharedNavigation === true
        height: visible ? implicitHeight : 0
        title: visible ? root.currentItem.title : ""
        showBackButton: visible && root.currentItem.showBackButton
        showCloseButton: visible && root.currentItem.showCloseButton
        onBackClicked: root.goBack()
        onCloseClicked: {
            if (root.currentItem) root.currentItem.closeClicked()
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
        }
    }

    Component {
        id: startPage
        OnboardingView {
            objectName: "walletCreationStartPage"
            showNavigationBar: false
            usesSharedNavigation: true
            title: qsTr("Add a wallet")
            maximumContentWidth: 640
            heading: qsTr("Your wallet starts here")
            subheading: qsTr("Create a wallet to manage your keys in this app.")
            showCloseButton: true
            primaryButtonText: qsTr("Create wallet")
            primaryButtonObjectName: "createWalletButton"
            primaryButtonEnabled: root.walletDiscoveryReady
            secondaryButtonText: qsTr("Import wallet")
            secondaryButtonObjectName: "importWalletButton"
            secondaryButtonEnabled: root.walletDiscoveryReady
            onCloseClicked: root.finished(false)
            onPrimaryClicked: root.push(typePage)
            onSecondaryClicked: root.push(legacyImport)

            childView: Item {
                implicitHeight: root.walletDiscoveryReady ? 0 : 48
                BusyIndicator {
                    objectName: "createWalletDiscoveryBusyIndicator"
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
            onCancel: root.finished(false)
            onRegularSelected: root.push(formPage, { "watchOnly": false,
                                                       "modalView": root.modalView })
            onWatchOnlySelected: root.push(formPage, { "watchOnly": true,
                                                         "modalView": root.modalView })
            onImportSelected: root.push(legacyImport)
            onExternalSignerSelected: {
                walletController.clearWalletLoadStatus()
                walletController.refreshExternalSignerStatus()
                root.push(externalWallet, {
                    "defaultWalletName": walletController.suggestedExternalSignerWalletName
                })
            }
        }
    }

    Component {
        id: formPage
        CreateWalletForm {
            creating: root.creatingWallet || root.readyPending
            onCancel: root.finished(false)
            onCreateRequested: function(name, xpub, passphrase) {
                root.completedWatchOnly = watchOnly
                root.completedEncrypted = !watchOnly && passphrase.length > 0
                root.creatingWallet = true
                if (watchOnly) walletController.createWatchOnlyWallet(name, xpub)
                else walletController.createSingleSigWallet(name, passphrase)
            }
        }
    }

    Component {
        id: readyPage
        WalletCreationReadyPage {
            onDone: root.finished(true)
        }
    }

    Component {
        id: externalWallet
        CreateExternalWallet {
            onBack: root.pop()
            onNext: root.push(externalConfirm)
        }
    }
    Component {
        id: legacyImport
        ImportWalletOptions {
            onBack: root.pop()
            onNext: root.push(legacyImportSuccess)
        }
    }
    Component {
        id: legacyImportSuccess
        ImportWalletSuccess {
            onBack: root.pop()
            onDone: root.finished(true)
            onViewSettings: {
                walletController.requestOpenWalletSettings()
                root.finished(true)
            }
        }
    }
    Component {
        id: externalConfirm
        CreateConfirm {
            pageObjectName: "externalWalletCreatedPage"
            headerText: qsTr("Your external wallet has been created")
            descriptionText: qsTr("This wallet uses the connected external signer for addresses and signing.")
            nextButtonText: qsTr("Done")
            nextButtonObjectName: "externalWalletCreatedDoneButton"
            onNext: root.finished(true)
        }
    }
}

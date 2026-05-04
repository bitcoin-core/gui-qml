// Copyright (c) 2024-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../../controls"
import "../../components"
import "../settings"
import "../wallet"

PageStack {
    id: root
    objectName: "createWalletWizard"

    enum Context { Onboarding, Main }

    signal finished()
    property string walletName: ""
    property string walletType: ""
    property string xpub: ""
    property int launchContext: CreateWalletWizard.Context.Onboarding
    property bool waitingForInit: false

    Component.onCompleted: {
        if (!walletController.initialized) {
            nodeModel.startNodeInitializionThread()
        }
        walletController.refreshExternalSignerStatus()
    }

    Connections {
        target: walletController
        enabled: root.waitingForInit
        function onInitializedChanged() {
            if (walletController.initialized) {
                root.waitingForInit = false
                walletController.createWatchOnlyWallet(root.walletName, root.xpub)
                if (walletController.isWalletLoaded) {
                    root.push(watchOnlyConfirm)
                }
            }
        }
    }
    onVisibleChanged: {
        if (visible) {
            walletController.refreshExternalSignerStatus()
        }
    }

    initialItem: Page {
        background: null
        property bool navigationBackEnabled: false

        header: NavigationBar2 {
            id: navbar
            leftItem: NavButton {
                objectName: "createWalletWizardBackButton"
                iconSource: "image://images/caret-left"
                text: qsTr("Back")
                onClicked: root.finished()
            }
            rightItem: NavButton {
                objectName: "createWalletWizardExitButton"
                text: {
                    switch (root.launchContext) {
                        case CreateWalletWizard.Context.Main:
                            return qsTr("Cancel");
                        case CreateWalletWizard.Context.Onboarding:
                        default:
                            return qsTr("Skip");
                    }
                }
                onClicked: {
                    root.finished()
                }
            }
        }

        ColumnLayout {
            id: columnLayout
            width: Math.min(parent.width, 450)
            anchors.horizontalCenter: parent.horizontalCenter

            Image {
                Layout.alignment: Qt.AlignCenter
                source: "image://images/add-wallet-dark"

                sourceSize.width: 200
                sourceSize.height: 200
            }

            Header {
                Layout.fillWidth: true
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                header: qsTr("Add a wallet")
                headerBold: true
                description: walletController.canCreateExternalSignerWallet
                    ? qsTr("Supported wallet types are external signer, watch-only, single-key,\nand multi-key.")
                    : qsTr("Supported wallet types are watch-only, single-key,\nand multi-key.")
            }

            ContinueButton {
                objectName: "createWalletButton"
                Layout.preferredWidth: Math.min(300, parent.width - 2 * Layout.leftMargin)
                Layout.topMargin: 40
                Layout.leftMargin: 20
                Layout.rightMargin: Layout.leftMargin
                Layout.bottomMargin: 20
                Layout.alignment: Qt.AlignCenter
                enabled: walletController.initialized
                text: qsTr("Create wallet")
                onClicked: {
                    root.push(typeSelector)
                }
            }

            ContinueButton {
                objectName: "importWalletButton"
                Layout.preferredWidth: Math.min(300, parent.width - 2 * Layout.leftMargin)
                Layout.leftMargin: 20
                Layout.rightMargin: Layout.leftMargin
                Layout.alignment: Qt.AlignCenter
                enabled: walletController.initialized
                text: qsTr("Import wallet")
                borderColor: Theme.color.neutral6
                borderHoverColor: Theme.color.orangeLight1
                borderPressedColor: Theme.color.orangeLight2
                textColor: Theme.color.orange
                backgroundColor: "transparent"
                backgroundHoverColor: "transparent"
                backgroundPressedColor: "transparent"
                onClicked: {
                    walletController.clearWalletLoadStatus()
                    root.push(import_options)
                }
            }

            ContinueButton {
                objectName: "createExternalWalletEntryButton"
                visible: walletController.canCreateExternalSignerWallet
                Layout.preferredWidth: Math.min(300, parent.width - 2 * Layout.leftMargin)
                Layout.leftMargin: 20
                Layout.rightMargin: Layout.leftMargin
                Layout.alignment: Qt.AlignCenter
                text: walletController.externalSignerName.length > 0
                    ? qsTr("Create external wallet")
                    : qsTr("Create hardware wallet")
                borderColor: Theme.color.neutral6
                borderHoverColor: Theme.color.orangeLight1
                borderPressedColor: Theme.color.orangeLight2
                textColor: Theme.color.orange
                backgroundColor: "transparent"
                backgroundHoverColor: "transparent"
                backgroundPressedColor: "transparent"
                onClicked: {
                    walletController.clearWalletLoadStatus()
                    walletController.refreshExternalSignerStatus()
                    root.push(external_wallet, {
                        "defaultWalletName": walletController.suggestedExternalSignerWalletName
                    })
                }
            }

            CoreText {
                visible: optionsModel.externalSignerPath.length > 0 && !walletController.canCreateExternalSignerWallet
                Layout.fillWidth: true
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                Layout.topMargin: 20
                wrapMode: Text.WordWrap
                color: Theme.color.neutral8
                text: {
                    if (walletController.externalSignerError.length > 0) {
                        return walletController.externalSignerError
                    }
                    return qsTr("No external signer is currently detected. Open Wallet settings to verify the signer path and rescan.")
                }
            }
        }
    }
    Component {
        id: typeSelector
        CreateTypeSelector {
            onBack: root.pop()
            onRegularSelected: {
                root.walletType = "singlesig"
                root.push(intro)
            }
            onWatchOnlySelected: {
                root.walletType = "watchonly"
                root.push(watchOnlyIntro)
            }
            onImportSelected: {
                walletController.clearWalletLoadStatus()
                root.push(import_options)
            }
        }
    }
    Component {
        id: watchOnlyIntro
        WatchOnlyIntro {
            onBack: root.pop()
            onNext: root.push(watchOnlyXpub)
        }
    }
    Component {
        id: watchOnlyXpub
        WatchOnlyXpub {
            id: xpubPage
            onBack: root.pop()
            onNext: {
                root.xpub = xpubPage.xpub
                root.push(name)
            }
        }
    }
    Component {
        id: watchOnlyConfirm
        CreateConfirm {
            headerText: qsTr("Your watch-only wallet has been created")
            descriptionText: qsTr("You can view transactions and balances. Spending requires an external signer. No backup is needed — your wallet can be recreated from the same extended public key.")
            onBack: root.pop()
            onNext: root.finished()
        }
    }
    Component {
        id: import_options
        ImportWalletOptions {
            onBack: root.goBack()
            onNext: root.push(import_success)
        }
    }
    Component {
        id: import_success
        ImportWalletSuccess {
            onBack: root.goBack()
            onDone: root.finished()
            onViewSettings: {
                walletController.requestOpenWalletSettings()
                root.finished()
            }
        }
    }
    Component {
        id: intro
        CreateIntro {
            onNext: root.push(name)
        }
    }
    Component {
        id: name
        CreateName {
            id: createName
            loading: root.waitingForInit
            onBack: root.pop()
            onNext: {
                root.walletName = createName.walletName
                if (root.walletType === "watchonly") {
                    if (walletController.initialized) {
                        walletController.createWatchOnlyWallet(root.walletName, root.xpub)
                        if (walletController.isWalletLoaded) {
                            root.push(watchOnlyConfirm)
                        }
                    } else {
                        root.waitingForInit = true
                    }
                } else {
                    root.push(password)
                }
            }
        }
    }
    Component {
        id: external_wallet
        CreateExternalWallet {
            onBack: root.pop()
            onNext: root.push(external_confirm)
        }
    }
    Component {
        id: password
        CreatePassword {
            walletName: root.walletName
            onNext: root.push(confirm)
        }
    }
    Component {
        id: confirm
        CreateConfirm {
            onNext: root.push(backup)
        }
    }
    Component {
        id: external_confirm
        CreateConfirm {
            pageObjectName: "externalWalletCreatedPage"
            headerText: qsTr("Your external wallet has been created")
            descriptionText: qsTr("This wallet uses the connected external signer for addresses and signing.")
            nextButtonText: qsTr("Done")
            nextButtonObjectName: "externalWalletCreatedDoneButton"
            onNext: root.finished()
        }
    }
    Component {
        id: backup
        CreateBackup {
            onNext: root.finished()
        }
    }
}

// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import Qt.labs.settings 1.0
import org.bitcoincore.qt 1.0
import "../components"
import "../controls"
import "./onboarding"
import "./node"
import "./wallet"

ApplicationWindow {
    id: appWindow
    title: qsTr("Bitcoin Core App")
    minimumWidth: 640
    minimumHeight: 665
    color: Theme.color.background
    visible: true

    Settings {
        property alias x: appWindow.x
        property alias y: appWindow.y
        property alias width: appWindow.width
        property alias height: appWindow.height
    }

    Behavior on color {
        ColorAnimation { duration: 150 }
    }

    PageStack {
        id: main
        objectName: "mainPageStack"
        initialItem: {
            if (needOnboarding) {
                onboardingWizard
            } else {
                if (AppMode.walletEnabled && AppMode.isDesktop) {
                    desktopWallets
                } else {
                    node
                }
            }
        }
        anchors.fill: parent
        focus: true
        Keys.onReleased: (event) => {
            if (event.key == Qt.Key_Back) {
                nodeModel.requestShutdown()
                event.accepted = true
            }
        }
    }

    Connections {
        target: nodeModel
        function onRequestedShutdown() {
            main.clear()
            main.push(shutdown)
        }
    }

    Component {
        id: onboardingWizard
        OnboardingWizard {
            onFinished: {
                optionsModel.onboard()
                nodeModel.startNodeInitializionThread()
                if (AppMode.walletEnabled && AppMode.isDesktop) {
                    // Start the node initialization before the wallet wizard is shown.
                    // DesktopWallets is pushed behind the wizard (lazy-loaded by StackView),
                    // so its Component.onCompleted does not fire until the wizard is dismissed.
                    // Starting early here ensures the wallet loader is ready by the time
                    // the user reaches the wallet creation step.
                    nodeModel.startNodeInitializionThread()
                    main.push([
                        desktopWallets, {},
                        createWalletWizard, { "launchContext": CreateWalletWizard.Context.Onboarding }
                    ])
                } else {
                    main.push(node)
                }
            }
        }
    }

    Component {
        id: desktopWallets
        DesktopWallets {
            onAddWallet: {
                main.push(createWalletWizard, { "launchContext": CreateWalletWizard.Context.Main })
            }
            onSendTransaction: (multipleRecipientsEnabled) => {
                if (multipleRecipientsEnabled) {
                    main.push(multipleSendReviewPage)
                } else {
                    main.push(sendReviewPage)
                }
            }
        }
    }

    Component {
        id: createWalletWizard
        CreateWalletWizard {
            onFinished: {
                main.pop()
            }
        }
    }

    Component {
        id: sendReviewPage
        SendReview {
            onBack: {
                main.pop()
            }
            onTransactionSent: {
                const externalSignerWallet = walletController.selectedWallet.hasExternalSigner
                const descriptionText = externalSignerWallet
                    ? qsTr("Approved on external signer. It should be confirmed within the next 10 minutes.")
                    : qsTr("Based on your selected fee, it should be confirmed within the next 10 minutes.")
                const actionText = externalSignerWallet ? qsTr("Done") : qsTr("Close window")
                walletController.selectedWallet.recipients.clear()
                main.push(sendResultPage, {
                    "descriptionText": descriptionText,
                    "actionText": actionText
                })
            }
        }
    }

    Component {
        id: multipleSendReviewPage
        MultipleSendReview {
            onBack: {
                main.pop()
            }
            onTransactionSent: {
                const externalSignerWallet = walletController.selectedWallet.hasExternalSigner
                const descriptionText = externalSignerWallet
                    ? qsTr("Approved on external signer. It should be confirmed within the next 10 minutes.")
                    : qsTr("Based on your selected fee, it should be confirmed within the next 10 minutes.")
                const actionText = externalSignerWallet ? qsTr("Done") : qsTr("Close window")
                walletController.selectedWallet.recipients.clear()
                main.push(sendResultPage, {
                    "descriptionText": descriptionText,
                    "actionText": actionText
                })
            }
        }
    }

    Component {
        id: sendResultPage
        SendResult {
            onDone: {
                main.pop(null)
            }
            onViewNewTransaction: {
                main.pop(null)
            }
        }
    }

    Component {
        id: shutdown
        Shutdown {}
    }

    Component.onCompleted: {
        if (!needOnboarding && AppMode.walletEnabled && AppMode.isDesktop) {
            nodeModel.startNodeInitializionThread()
        }
    }

    Component {
        id: node
        PageStack {
            id: nodeStack
            vertical: true
            initialItem: node
            Component {
                id: node
                NodeRunner {
                    onSettingsClicked: {
                        nodeStack.push(nodeSettings)
                    }
                }
            }
            Component {
                id: nodeSettings
                 NodeSettings {
                    onDoneClicked: {
                        nodeStack.pop()
                    }
                }
            }
        }
    }
}

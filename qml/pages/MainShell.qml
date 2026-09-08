// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15

import org.bitcoincore.qt 1.0

import "../controls"
import "./node"
import "./wallet"

Page {
    id: root
    objectName: "mainShellPage"
    background: Rectangle { color: Theme.color.background }

    signal addWallet()

    function _maybeLoadWallets() {
        if (AppMode.walletEnabled && walletController && walletController.initialized) {
            walletListModel.listWalletDir()
        }
    }

    Component.onCompleted: _maybeLoadWallets()

    Connections {
        target: walletController
        function onInitializedChanged() { root._maybeLoadWallets() }
    }

    TabView {
        anchors.fill: parent

        TabSection {
            title: qsTr("Node")
            Tab {
                title: qsTr("Node")
                iconSource: "image://images/tab-node"
                activeIconSource: "image://images/tab-node-filled"
                contentComponent: Component { NodeRunner { showNavigationButtons: false } }
            }
            Tab {
                title: qsTr("Peers")
                iconSource: "image://images/tab-peers"
                activeIconSource: "image://images/tab-peers-filled"
                displayModes: TabView.Sidebar
                contentComponent: Component { Peers { showBackButton: false } }
            }
            Tab {
                title: qsTr("Network")
                iconSource: "image://images/network-light"
                activeIconSource: "image://images/network-light"
                displayModes: TabView.Sidebar
                contentComponent: Component { NetworkTraffic { showBackButton: false } }
            }
            Tab {
                title: qsTr("RPC Console")
                iconSource: "image://images/tab-console"
                activeIconSource: "image://images/tab-console-filled"
                displayModes: TabView.Sidebar
                contentComponent: Component {
                    CommandConsole {
                        showHeader: false
                        tabActive: visible
                        walletName: walletController.isWalletLoaded && walletController.selectedWallet
                            ? walletController.selectedWallet.name
                            : ""
                    }
                }
            }
        }

        TabSection {
            title: qsTr("Wallets")
            enabled: AppMode.walletEnabled
            displayModes: TabView.Sidebar
            model: walletListModel
            delegate: Tab {
                required property string name
                required property string displayName
                required property string format
                required property int loadState
                required property int keySchemeKind
                readonly property string walletIconSource: {
                    const filled = loadState === WalletListModel.Open
                    if (keySchemeKind === WalletQmlModel.WatchOnly) {
                        return filled ? "image://images/visible-filled" : "image://images/visible"
                    }
                    if (keySchemeKind === WalletQmlModel.MultiKey) {
                        return filled ? "image://images/two-keys-filled" : "image://images/two-keys"
                    }
                    return filled ? "image://images/key-filled" : "image://images/key"
                }
                title: displayName
                dimmed: loadState !== WalletListModel.Open
                iconSource: walletIconSource
                activeIconSource: walletIconSource
                onSelected: walletController.setSelectedWallet(name, format)
                contentComponent: Component { WalletContainer {} }
            }
            Tab {
                title: qsTr("Add Wallet")
                iconSource: "image://images/tab-add"
                activeIconSource: "image://images/tab-add-filled"
                actionOnly: true
                onTriggered: addWalletPopup.open()
            }
        }

        Tab {
            title: qsTr("Wallet")
            iconSource: "image://images/tab-wallet"
            activeIconSource: "image://images/tab-wallet-filled"
            displayModes: TabView.TabBar
            enabled: AppMode.walletEnabled
            contentComponent: Component { WalletContainer {} }
        }

        Tab {
            title: qsTr("Settings")
            iconSource: "image://images/tab-settings"
            activeIconSource: "image://images/tab-settings-filled"
            pinToBottom: true
            contentComponent: Component { NodeSettings { showDoneButton: false } }
        }
    }

    Popup {
        id: addWalletPopup
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        anchors.centerIn: parent
        width: Math.min(parent.width - 40, 720)
        height: Math.min(parent.height - 40, 720)
        padding: 0

        background: Rectangle {
            color: Theme.color.neutral1
            radius: 16
            border.width: 1
            border.color: Theme.color.neutral2
        }

        contentItem: CreateWalletWizard {
            clip: true
            launchContext: CreateWalletWizard.Context.Main
            onFinished: {
                addWalletPopup.close()
                walletListModel.listWalletDir()
            }
        }
    }
}

// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import org.bitcoincore.qt 1.0

import "../../controls"
import "../../controls/utils.js" as Utils
import "../../components"
import "../node"

Page {
    id: root
    background: null

    ButtonGroup { id: navigationTabs }

    signal addWallet()
    signal sendTransaction(bool multipleRecipientsEnabled)

    header: NavigationBar2 {
        id: navBar
        leftItem: WalletBadge {
            implicitWidth: 154
            implicitHeight: 46
            text: walletController.selectedWallet.name
            balance: walletController.selectedWallet.balance
            loading: !walletController.initialized

            MouseArea {
                anchors.fill: parent
                onClicked: {
                    walletListModel.listWalletDir()
                    walletSelect.opened ? walletSelect.close() : walletSelect.open()
                }
            }

            WalletSelect {
                id: walletSelect
                model: walletListModel
                closePolicy: Popup.CloseOnPressOutside
                x: 0
                y: parent.height

                onAddWallet: {
                    root.addWallet()
                }
            }
        }
        centerItem: RowLayout {
            NavigationTab {
                id: activityTabButton
                checked: true
                text: qsTr("Activity")
                property int index: 0
                ButtonGroup.group: navigationTabs
            }
            NavigationTab {
                text: qsTr("Send")
                property int index: 1
                ButtonGroup.group: navigationTabs
            }
            NavigationTab {
                text: qsTr("Receive")
                property int index: 2
                ButtonGroup.group: navigationTabs
            }
        }
        rightItem: RowLayout {
            spacing: 5
            NetworkIndicator {
                textSize: 11
                Layout.rightMargin: 5
                shorten: true
            }
            NavigationTab {
                id: blockClockTabButton
                Layout.preferredWidth: 30
                Layout.rightMargin: 10
                property int index: 3
                ButtonGroup.group: navigationTabs

                Tooltip {
                    id: blockClockTooltip
                    property var syncState: Utils.formatRemainingSyncTime(nodeModel.remainingSyncTime)
                    property bool synced: nodeModel.verificationProgress > 0.9999
                    property bool paused: nodeModel.pause
                    property bool connected: nodeModel.numOutboundPeers > 0

                    anchors.top: blockClockTabButton.bottom
                    anchors.topMargin: -5
                    anchors.horizontalCenter: blockClockTabButton.horizontalCenter

                    visible: blockClockTabButton.hovered
                    text: {
                        if (paused) {
                            qsTr("Paused")
                        } else if (connected && synced) {
                            qsTr("Blocktime\n" +  Number(nodeModel.blockTipHeight).toLocaleString(Qt.locale(), 'f', 0))
                        } else if (connected){
                            qsTr("Downloading blocks\n" +  syncState.text)
                        } else {
                            qsTr("Connecting")
                        }
                    }
                }
            }
            NavigationTab {
                iconSource: "image://images/gear-outline"
                iconColor: Theme.color.neutral7
                Layout.preferredWidth: 30
                property int index: 4
                ButtonGroup.group: navigationTabs
            }
        }
        background: Rectangle {
            color: Theme.color.neutral4
            anchors.bottom: navBar.bottom
            anchors.bottomMargin: 4
            height: 1
            width: parent.width
        }
    }

    contentItem: StackLayout {
        currentIndex: navigationTabs.checkedButton.index
        clip: true
        Activity {
        }
        Send {
            onTransactionPrepared: root.sendTransaction(multipleRecipientsEnabled)
        }
        RequestPayment {
        }
        Item {
            id: blockClockTab
            BlockClock {
                parentWidth: blockClockTab.width - 40
                parentHeight: blockClockTab.height
                anchors.centerIn: blockClockTab
                showNetworkIndicator: false
            }
        }
        NodeSettings {
            showDoneButton: false
        }
    }

    Component.onCompleted: nodeModel.startNodeInitializionThread();
}

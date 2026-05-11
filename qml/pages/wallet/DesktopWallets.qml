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

    property string pendingMigrationPath: ""

    ButtonGroup { id: navigationTabs }

    signal addWallet()
    signal sendTransaction(bool multipleRecipientsEnabled)

    function handleWalletMigrationRequired(walletPath) {
        const stackView = root.StackView.view
        if (!stackView || stackView.currentItem !== root) {
            return
        }

        walletSelect.close()
        stackView.push(walletMigrationPage, { "walletPath": walletPath })
    }

    function handleWalletBadgeClicked() {
        if (!walletController.initialized) {
            return
        }

        walletListModel.listWalletDir()
        if (walletController.noWalletsFound) {
            root.addWallet()
        } else {
            walletSelect.opened ? walletSelect.close() : walletSelect.open()
        }
    }

    Component {
        id: walletMigrationPage
        ImportWalletMigration {
            onBack: root.StackView.view.pop()
            onNext: root.StackView.view.pop()
        }
    }

    Connections {
        target: walletController
        function onOpenWalletSettingsRequested() {
            settingsTabButton.checked = true
            nodeSettings.openWalletSettings()
        }
        function onWalletMigrationRequired(path) {
            root.pendingMigrationPath = path
            migrationRequiredPopup.errorText = ""
            migrationRequiredPopup.open()
        }
        function onWalletMigrationSucceeded() {
            root.pendingMigrationPath = ""
            migrationRequiredPopup.close()
            migrationPassphrasePopup.close()
        }
        function onWalletMigrationFailed() {
            if (root.pendingMigrationPath.length === 0) {
                return
            }
            if (walletController.walletMigrationError.toLowerCase().indexOf("passphrase") !== -1) {
                const showPassphraseError = migrationPassphrasePopup.opened
                migrationRequiredPopup.close()
                migrationPassphrasePopup.busy = false
                migrationPassphrasePopup.errorText = showPassphraseError ? walletController.walletMigrationError : ""
                migrationPassphrasePopup.open()
            } else {
                migrationRequiredPopup.busy = false
                migrationRequiredPopup.errorText = walletController.walletMigrationError
                migrationRequiredPopup.open()
            }
        }
    }

    header: NavigationBar2 {
        id: navBar
        leftItem: WalletBadge {
            objectName: "walletBadge"
            implicitWidth: 154
            implicitHeight: 46
            text: walletController.selectedWallet.name
            balance: walletController.selectedWallet.balance
            balanceSatoshi: walletController.selectedWallet.balanceSatoshi
            loading: !walletController.initialized
            noWalletLoaded: !walletController.isWalletLoaded
            noWalletsFound: walletController.noWalletsFound
            onClicked: root.handleWalletBadgeClicked()

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
            visible: walletController.isWalletLoaded
            NavigationTab {
                id: activityTabButton
                objectName: "desktopWalletsActivityTab"
                text: qsTr("Activity")
                property int index: 0
                ButtonGroup.group: navigationTabs
            }
            NavigationTab {
                objectName: "desktopWalletsSendTab"
                text: qsTr("Send")
                property int index: 1
                ButtonGroup.group: navigationTabs
            }
            NavigationTab {
                objectName: "desktopWalletsReceiveTab"
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
                checked: true
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
                id: settingsTabButton
                objectName: "desktopWalletSettingsTabButton"
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
            onTransactionPrepared: (multipleRecipientsEnabled) => {
                root.sendTransaction(multipleRecipientsEnabled)
            }
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
            id: nodeSettings
            showDoneButton: false
        }
    }

    WalletMigrationPopup {
        id: migrationRequiredPopup
        parent: Overlay.overlay
        width: Math.min(420, root.width - 40)
        popupObjectName: "walletMigrationPopup"
        errorTextObjectName: "walletMigrationErrorText"
        cancelButtonObjectName: "walletMigrationCancelButton"
        confirmButtonObjectName: "walletMigrationConfirmButton"
        descriptionText: qsTr("This wallet uses a legacy format and needs to be updated before it can be opened.")
        busy: walletController.walletMigrationInProgress
        onConfirmed: {
            migrationRequiredPopup.errorText = ""
            migrationRequiredPopup.close()
            walletController.migrateWallet(root.pendingMigrationPath, "")
        }
    }

    WalletPassphrasePopup {
        id: migrationPassphrasePopup
        parent: Overlay.overlay
        width: Math.min(420, root.width - 40)
        popupObjectName: "walletMigrationPassphrasePopup"
        passphraseFieldObjectName: "walletMigrationPassphraseField"
        errorTextObjectName: "walletMigrationPassphraseErrorText"
        cancelButtonObjectName: "walletMigrationPassphraseCancelButton"
        confirmButtonObjectName: "walletMigrationPassphraseConfirmButton"
        titleText: qsTr("Enter wallet password")
        descriptionText: qsTr("Enter the wallet password to complete the legacy wallet update.")
        confirmText: qsTr("Unlock and update")
        busyConfirmText: qsTr("Updating...")
        onSubmitted: (passphrase) => {
            migrationPassphrasePopup.busy = true
            walletController.migrateWallet(root.pendingMigrationPath, passphrase)
        }
    }
}

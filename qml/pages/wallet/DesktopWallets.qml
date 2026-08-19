// Copyright (c) 2024-2026 The Bitcoin Core developers
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

    function navigateToTransaction(txid, outputIndex) {
        activityTabButton.checked = true
        activityPage.navigateToTransaction(txid, outputIndex)
    }

    function toggleWalletSelection() {
        if (!walletController.initialized) {
            return
        }
        if (walletSelect.opened) {
            walletSelect.close()
            return
        }
        walletListModel.listWalletDir()
        if (walletController.noWalletsFound) {
            root.addWallet()
        } else {
            walletSelect.open()
        }
    }

    function openWalletSelection() {
        if (!walletController.initialized) {
            return
        }
        walletListModel.listWalletDir()
        if (walletController.noWalletsFound) {
            root.addWallet()
        } else {
            walletSelect.open()
        }
    }

    function openSettingsRoute(route) {
        settingsLoader.pendingRoute = route
        settingsTabButton.checked = true
        Qt.callLater(settingsLoader.applyPendingRoute)
    }

    Connections {
        target: walletController
        function onOpenWalletSettingsRequested() {
            root.openSettingsRoute("wallet")
        }
        function onOpenReceiveRequested() {
            receiveTabButton.checked = true
        }
        function onWalletLoadStateChanged(name, state, error) {
            if (state === WalletListModel.LoadError) {
                loadErrorPopup.message = error
                loadErrorPopup.open()
            }
        }
        function onWalletMigrationRequired(path) {
            root.pendingMigrationPath = path
            migrationRequiredPopup.errorText = ""
            migrationRequiredPopup.open()
        }
        function onWalletMigrationPassphraseRequired(path) {
            root.pendingMigrationPath = path
            migrationRequiredPopup.close()
            migrationPassphrasePopup.busy = false
            migrationPassphrasePopup.errorText = ""
            migrationPassphrasePopup.open()
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
                migrationPassphrasePopup.busy = false
                migrationPassphrasePopup.close()
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
            implicitWidth: 175
            implicitHeight: 46
            text: walletController.selectedWallet.displayName
            balance: walletController.selectedWallet.balance
            balanceSatoshi: walletController.selectedWallet.balanceSatoshi
            loading: !walletController.initialized
            noWalletLoaded: !walletController.isWalletLoaded
            noWalletsFound: walletController.noWalletsFound
            keySchemeKind: walletController.selectedWallet.keySchemeKind

            onClicked: {
                root.toggleWalletSelection()
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
                onCloseWalletRequested: (name) => {
                    closeConfirmationPopup.walletName = name
                    closeConfirmationPopup.open()
                }
            }
        }
        centerItem: RowLayout {
            visible: walletController.isWalletLoaded
            NavigationTab {
                id: activityTabButton
                objectName: "activityTabButton"
                text: qsTr("Activity")
                property int index: 0
                ButtonGroup.group: navigationTabs
            }
            NavigationTab {
                objectName: "sendTabButton"
                text: qsTr("Send")
                property int index: 1
                ButtonGroup.group: navigationTabs
            }
            NavigationTab {
                id: receiveTabButton
                objectName: "receiveTabButton"
                text: qsTr("Receive")
                property int index: 2
                ButtonGroup.group: navigationTabs
            }
        }
        rightItem: RowLayout {
            spacing: 5
            NetworkIndicator {
                textSize: 11
                shorten: true
            }
            NavigationTab {
                id: blockClockTabButton
                objectName: "blockClockTabButton"
                checked: true
                Layout.preferredWidth: 30
                property int index: 3
                ButtonGroup.group: navigationTabs
                customContent: MiniBlockClock {
                    pageSelected: blockClockTabButton.checked
                }

                Tooltip {
                    id: blockClockTooltip
                    property var syncState: Utils.formatRemainingSyncTime(nodeModel.remainingSyncTime)
                    property bool synced: nodeModel.verificationProgress > 0.999
                    property bool paused: nodeModel.pause
                    property bool connected: nodeModel.numPeers > 0
                    property bool faulted: nodeModel.faulted
                    property bool offline: typeof networkStatusModel !== "undefined" && networkStatusModel.networkOffline
                    property bool headerSyncActive: nodeModel.headerSyncActive

                    anchors.top: blockClockTabButton.bottom
                    anchors.topMargin: -5
                    anchors.horizontalCenter: blockClockTabButton.horizontalCenter

                    visible: blockClockTabButton.hovered
                    text: {
                        if (faulted) {
                            qsTr("Error")
                        } else if (offline) {
                            qsTr("Offline")
                        } else if (paused) {
                            qsTr("Paused")
                        } else if (connected && synced) {
                            qsTr("Blocktime\n" +  Number(nodeModel.blockTipHeight).toLocaleString(Qt.locale(), 'f', 0))
                        } else if (connected && headerSyncActive) {
                            nodeModel.headerPresync ? qsTr("Pre-syncing headers") : qsTr("Syncing headers")
                        } else if (connected) {
                            qsTr("Downloading blocks\n" +  syncState.text)
                        } else {
                            qsTr("Connecting")
                        }
                    }
                }
            }
            NavigationTab {
                id: peersTabButton
                objectName: "peersTabButton"
                iconSource: Utils.nodeConnectionIcon(nodeModel.numPeers)
                iconColor: Theme.color.neutral7
                iconSize: 24
                Layout.preferredWidth: 30
                property int index: 4
                ButtonGroup.group: navigationTabs
                onCheckedChanged: {
                    if (checked) {
                        peerTableModel.startAutoRefresh()
                    } else {
                        peerTableModel.stopAutoRefresh()
                        if (peersStack.depth > 1) peersStack.pop(null)
                    }
                }

                Tooltip {
                    anchors.top: peersTabButton.bottom
                    anchors.topMargin: -5
                    anchors.horizontalCenter: peersTabButton.horizontalCenter
                    visible: peersTabButton.hovered
                    text: qsTr("Peers")
                }
            }
            NavigationTab {
                id: settingsTabButton
                objectName: "desktopWalletSettingsTabButton"
                iconSource: "image://images/gear-outline"
                iconColor: Theme.color.neutral7
                Layout.preferredWidth: 30
                property int index: 5
                ButtonGroup.group: navigationTabs

                Tooltip {
                    anchors.top: settingsTabButton.bottom
                    anchors.topMargin: -5
                    anchors.horizontalCenter: settingsTabButton.horizontalCenter
                    visible: settingsTabButton.hovered
                    text: qsTr("Settings")
                }
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
            id: activityPage
        }
        Send {
            onTransactionPrepared: (multipleRecipientsEnabled) => {
                root.sendTransaction(multipleRecipientsEnabled)
            }
            onViewTransactionInActivity: (txid) => {
                root.navigateToTransaction(txid)
            }
        }
        RequestPayment {
            onAddressHistoryRequested: {
                root.openSettingsRoute("addresses")
            }
        }
        Item {
            id: blockClockTab
            NodeStatusActions {
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.topMargin: 16
                anchors.rightMargin: 16
                z: 2
            }
            BlockClock {
                parentWidth: blockClockTab.width - 40
                parentHeight: blockClockTab.height
                anchors.centerIn: blockClockTab
                showNetworkIndicator: false
            }
        }
        PageStack {
            id: peersStack
            initialItem: Peers {
                showBackButton: false
                onPeerSelected: (peerDetails) => {
                    peersStack.push(peerDetailsComp, {"details": peerDetails})
                }
                onBannedPeers: {
                    peersStack.push(bannedPeersComp)
                }
            }
            Component {
                id: peerDetailsComp
                PeerDetails {
                    onBack: peersStack.pop()
                }
            }
            Component {
                id: bannedPeersComp
                BannedPeers {
                    onBack: peersStack.pop()
                }
            }
        }
        Item {
            Loader {
                id: settingsLoader
                objectName: "settingsLoader"
                anchors.fill: parent
                property bool retainItem: false
                property string pendingRoute: ""

                function applyPendingRoute() {
                    if (!item || pendingRoute.length === 0) return
                    if (pendingRoute === "addresses") item.openWalletAddressHistory()
                    else item.selectSection(pendingRoute)
                    pendingRoute = ""
                }

                // Create Settings on first use, then retain its navigation
                // stacks while the parent tab item is hidden.
                active: settingsTabButton.checked || retainItem
                onLoaded: {
                    retainItem = true
                    Qt.callLater(applyPendingRoute)
                }
                sourceComponent: SettingsView {
                    showDoneButton: false
                    onSelectWalletRequested: root.openWalletSelection()
                    onReceiveRequested: receiveTabButton.checked = true
                }
            }
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

    AlertPopup {
        id: loadErrorPopup
        objectName: "walletLoadErrorPopup"
        parent: Overlay.overlay
        width: Math.min(420, root.width - 40)
        title: qsTr("Failed to open wallet")
        messageObjectName: "walletLoadErrorPopupText"

        AlertAction {
            text: qsTr("OK")
            buttonObjectName: "walletLoadErrorPopupDismissButton"
        }
    }

    AlertPopup {
        id: closeConfirmationPopup
        objectName: "walletCloseConfirmationPopup"
        parent: Overlay.overlay
        width: Math.min(420, root.width - 40)
        title: qsTr("Close wallet")

        property string walletName: ""
        message: qsTr("Do you want to close the wallet \"%1\"?").arg(walletName)

        AlertAction {
            text: qsTr("Cancel")
            role: AlertAction.Cancel
            buttonObjectName: "walletCloseConfirmationCancelButton"
        }
        AlertAction {
            text: qsTr("Close wallet")
            buttonObjectName: "walletCloseConfirmationConfirmButton"
            onTriggered: walletController.closeWallet(closeConfirmationPopup.walletName)
        }
    }
}

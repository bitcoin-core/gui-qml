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
    // The wallet shown in the window. Wallet pages never look this up
    // themselves; each WalletView gets its wallet when it is created.
    readonly property var activeWallet: ApplicationModels.walletController.isWalletLoaded ? ApplicationModels.walletController.selectedWallet : null
    readonly property WalletView activeView: {
        walletViews.count
        return root.viewForWallet(root.activeWallet)
    }
    readonly property bool walletTabSelected: navigationTabs.checkedButton !== null
        && (navigationTabs.checkedButton as WalletNavigationTab).index <= 2
    readonly property Item walletSettingsHost: settingsLoader.item ? (settingsLoader.item as SettingsView).walletSettingsHost : null
    readonly property alias walletViews: walletViews

    ButtonGroup { id: navigationTabs }

    signal addWallet()

    function viewForWallet(wallet) {
        if (!wallet) return null
        for (let index = 0; index < walletViews.count; ++index) {
            const view = walletViews.itemAt(index) as WalletView
            if (view && view.wallet === wallet) return view
        }
        return null
    }

    // Mirror walletController.openWallets in a ListModel so the Repeater
    // creates or destroys only the views of wallets that opened or closed.
    // A wallet that is unloading loses its view at once, before the
    // controller drops it from the list.
    function syncOpenWallets() {
        const wallets = ApplicationModels.walletController.openWallets.filter(function(wallet) {
            return wallet.available !== false
        })
        for (let index = openWalletsModel.count - 1; index >= 0; --index) {
            if (wallets.indexOf(openWalletsModel.get(index).wallet) < 0) openWalletsModel.remove(index)
        }
        for (let index = 0; index < wallets.length; ++index) {
            let known = false
            for (let row = 0; row < openWalletsModel.count; ++row) {
                if (openWalletsModel.get(row).wallet === wallets[index]) {
                    known = true
                    break
                }
            }
            if (!known) openWalletsModel.append({ "wallet": wallets[index] })
        }
    }

    function walletTabButton(tab) {
        if (tab === WalletView.Send) return sendTabButton
        if (tab === WalletView.Receive) return receiveTabButton
        return activityTabButton
    }

    function showWalletTab(tab) {
        root.walletTabButton(tab).checked = true
    }

    function showActiveViewTab() {
        if (!root.walletTabSelected) return
        if (root.activeView) {
            root.showWalletTab(root.activeView.currentTab)
        } else if (walletViews.count === 0) {
            // Leave the wallet tabs only when no wallet is left to show. While
            // the selected wallet unloads, another open wallet is selected next.
            blockClockTabButton.checked = true
        }
    }

    function walletTabChecked(tab, checked) {
        if (checked && root.activeView) root.activeView.currentTab = tab
    }

    onActiveViewChanged: root.showActiveViewTab()
    Component.onCompleted: root.syncOpenWallets()

    function navigateToTransaction(txid, outputIndex) {
        if (root.activeView) root.activeView.navigateToTransaction(txid, outputIndex)
    }

    function requestCloseWallet() {
        if (!root.activeWallet) {
            return
        }
        closeConfirmationPopup.walletName = root.activeWallet.name
        closeConfirmationPopup.open()
    }

    function startWalletBackup() {
        root.openSettingsRoute("backup")
    }

    function openWalletPassword(updating) {
        root.openSettingsRoute("password", updating)
    }

    function openSignVerifyMessage(initialTab) {
        root.openSettingsRoute("sign-verify-message", initialTab)
    }

    function openSettings(section) {
        root.openSettingsRoute(section)
    }

    function openUriImporter() {
        if (root.activeView) root.activeView.openUriImporter()
    }

    function openPsbtImporter() {
        if (root.activeView) root.activeView.openPsbtImporter()
    }

    function openConsole() {
        root.openSettings("rpc-console")
    }

    function openPeers() {
        peersTabButton.checked = true
    }

    function openNetworkTraffic() {
        root.openSettings("network-traffic")
    }

    function openNode() {
        blockClockTabButton.checked = true
    }

    // Show a wallet's Activity, as long as that wallet is still open.
    function openWallet(wallet) {
        if (!wallet || wallet.available === false || ApplicationModels.walletController.openWallets.indexOf(wallet) < 0) return
        if (ApplicationModels.walletController.selectedWallet !== wallet) ApplicationModels.walletController.setSelectedWallet(wallet.name)
        if (root.activeWallet === wallet) root.showWalletTab(WalletView.Activity)
    }

    function openActivity() {
        if (root.activeView) root.showWalletTab(WalletView.Activity)
    }

    function openSend() {
        if (root.activeView) root.showWalletTab(WalletView.Send)
    }

    function openReceive() {
        if (root.activeView) root.showWalletTab(WalletView.Receive)
    }

    function toggleWalletSelection() {
        if (!ApplicationModels.walletController.initialized) {
            return
        }
        if (walletSelect.visible) {
            walletSelect.close()
            return
        }
        ApplicationModels.walletListModel.listWalletDir()
        if (ApplicationModels.walletController.noWalletsFound) {
            root.addWallet()
        } else {
            walletSelect.open()
        }
    }

    function openWalletSelection() {
        if (!ApplicationModels.walletController.initialized) {
            return
        }
        ApplicationModels.walletListModel.listWalletDir()
        if (ApplicationModels.walletController.noWalletsFound) {
            root.addWallet()
        } else {
            walletSelect.open()
        }
    }

    // Wallet routes go to the active wallet's view, which shows its own
    // settings stack in the wallet section of the shared settings page.
    function openSettingsRoute(route, argument) {
        const walletRoutes = ["wallet", "addresses", "backup", "password", "sign-verify-message"]
        if (walletRoutes.indexOf(route) >= 0 && route !== "wallet" && root.activeView) {
            root.activeView.openSettingsRoute(route, argument)
            return
        }
        root.showSettingsSection(walletRoutes.indexOf(route) >= 0 ? "wallet" : route)
    }

    function showSettingsSection(section) {
        settingsLoader.pendingRoute = section
        settingsTabButton.checked = true
        Qt.callLater(settingsLoader.applyPendingRoute)
    }

    ListModel { id: openWalletsModel }

    Connections {
        target: ApplicationModels.walletController
        function onOpenWalletsChanged() {
            root.syncOpenWallets()
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
            if (ApplicationModels.walletController.walletMigrationError.toLowerCase().indexOf("passphrase") !== -1) {
                const showPassphraseError = migrationPassphrasePopup.opened
                migrationRequiredPopup.close()
                migrationPassphrasePopup.busy = false
                migrationPassphrasePopup.errorText = showPassphraseError ? ApplicationModels.walletController.walletMigrationError : ""
                migrationPassphrasePopup.open()
            } else {
                migrationPassphrasePopup.busy = false
                migrationPassphrasePopup.close()
                migrationRequiredPopup.busy = false
                migrationRequiredPopup.errorText = ApplicationModels.walletController.walletMigrationError
                migrationRequiredPopup.open()
            }
        }
    }

    header: NavigationBar2 {
        id: navBar
        leftItem: WalletBadge {
            objectName: "walletBadge"
            implicitHeight: 48
            checked: walletSelect.visible
            walletType: ApplicationModels.walletController.selectedWallet.keyScheme
            text: ApplicationModels.walletController.selectedWallet.displayName
            balance: ApplicationModels.walletController.selectedWallet.balance
            balanceSatoshi: ApplicationModels.walletController.selectedWallet.balanceSatoshi
            loading: !ApplicationModels.walletController.initialized
            noWalletLoaded: !ApplicationModels.walletController.isWalletLoaded
            noWalletsFound: ApplicationModels.walletController.noWalletsFound
            keySchemeKind: ApplicationModels.walletController.selectedWallet.keySchemeKind

            onClicked: {
                root.toggleWalletSelection()
            }

            WalletSelect {
                id: walletSelect
                model: ApplicationModels.walletListModel
                closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
                width: Math.max(360, parent.width)
                x: 0
                y: parent.height + 2 + verticalOffset

                onAddWallet: {
                    root.addWallet()
                }
                onWalletSettingsRequested: (name, format) => {
                    ApplicationModels.walletController.setSelectedWallet(name, format)
                    root.openSettingsRoute("wallet")
                }
                onCloseWalletRequested: (name) => {
                    closeConfirmationPopup.walletName = name
                    closeConfirmationPopup.open()
                }
            }
        }
        centerItem: RowLayout {
            visible: ApplicationModels.walletController.isWalletLoaded
            WalletNavigationTab {
                id: activityTabButton
                objectName: "activityTabButton"
                text: qsTr("Activity")
                index: 0
                ButtonGroup.group: navigationTabs
                onCheckedChanged: root.walletTabChecked(WalletView.Activity, checked)
            }
            WalletNavigationTab {
                id: sendTabButton
                objectName: "sendTabButton"
                text: qsTr("Send")
                index: 1
                ButtonGroup.group: navigationTabs
                onCheckedChanged: root.walletTabChecked(WalletView.Send, checked)
            }
            WalletNavigationTab {
                id: receiveTabButton
                objectName: "receiveTabButton"
                text: qsTr("Receive")
                index: 2
                ButtonGroup.group: navigationTabs
                onCheckedChanged: root.walletTabChecked(WalletView.Receive, checked)
            }
        }
        rightItem: RowLayout {
            spacing: 5
            NetworkIndicator {
                textSize: 11
                shorten: true
            }
            WalletNavigationTab {
                id: blockClockTabButton
                objectName: "blockClockTabButton"
                checked: true
                Layout.preferredWidth: 30
                index: 3
                ButtonGroup.group: navigationTabs
                customContent: MiniBlockClock {
                    pageSelected: blockClockTabButton.checked
                    renderingActive: root.visible
                }

                Tooltip {
                    id: blockClockTooltip
                    objectName: "blockClockTooltip"
                    property var syncState: Utils.formatRemainingSyncTime(ApplicationModels.nodeModel.remainingSyncTime)
                    property bool synced: ApplicationModels.nodeModel.initialSyncComplete
                    property bool paused: ApplicationModels.nodeModel.pause
                    property bool connected: ApplicationModels.nodeModel.numPeers > 0
                    property bool faulted: ApplicationModels.nodeModel.faulted
                    property bool offline: typeof networkStatusModel !== "undefined" && ApplicationModels.networkStatusModel.networkOffline
                    property bool headerSyncActive: ApplicationModels.nodeModel.headerSyncActive

                    anchors.top: blockClockTabButton.bottom
                    anchors.topMargin: 8
                    anchors.horizontalCenter: blockClockTabButton.horizontalCenter

                    shown: blockClockTabButton.hovered
                    text: {
                        if (faulted) {
                            qsTr("Error")
                        } else if (offline) {
                            qsTr("Offline")
                        } else if (paused) {
                            qsTr("Paused")
                        } else if (connected && synced) {
                            qsTr("Blocktime\n" +  Number(ApplicationModels.nodeModel.blockTipHeight).toLocaleString(Qt.locale(), 'f', 0))
                        } else if (connected && headerSyncActive) {
                            ApplicationModels.nodeModel.headerPresync ? qsTr("Pre-syncing headers") : qsTr("Syncing headers")
                        } else if (connected) {
                            qsTr("Downloading blocks\n" +  syncState.text)
                        } else {
                            qsTr("Connecting")
                        }
                    }
                }
            }
            WalletNavigationTab {
                id: peersTabButton
                objectName: "peersTabButton"
                iconSource: Utils.nodeConnectionIcon(ApplicationModels.nodeModel.numPeers)
                iconColor: Theme.color.neutral7
                iconSize: 24
                Layout.preferredWidth: 30
                index: 4
                ButtonGroup.group: navigationTabs
                onCheckedChanged: {
                    if (checked) {
                        ApplicationModels.peerTableModel.startAutoRefresh()
                    } else {
                        ApplicationModels.peerTableModel.stopAutoRefresh()
                    }
                }

                Tooltip {
                    anchors.top: peersTabButton.bottom
                    anchors.topMargin: 8
                    anchors.horizontalCenter: peersTabButton.horizontalCenter
                    shown: peersTabButton.hovered
                    text: qsTr("Peers")
                }
            }
            WalletNavigationTab {
                id: settingsTabButton
                objectName: "desktopWalletSettingsTabButton"
                iconSource: "image://images/gear-outline"
                iconColor: Theme.color.neutral7
                Layout.preferredWidth: 30
                index: 5
                ButtonGroup.group: navigationTabs

                Tooltip {
                    anchors.top: settingsTabButton.bottom
                    anchors.topMargin: 8
                    anchors.horizontalCenter: settingsTabButton.horizontalCenter
                    shown: settingsTabButton.hovered
                    text: qsTr("Settings")
                }
            }
        }
        background: Rectangle {
            color: Theme.color.neutral2
            anchors.bottom: navBar.bottom
            anchors.bottomMargin: 4
            height: 1
            width: parent.width
        }
    }

    contentItem: Item {
        clip: true

        Item {
            id: walletViewsHost
            objectName: "walletViewsHost"
            anchors.fill: parent
            visible: root.walletTabSelected

            Repeater {
                id: walletViews
                model: openWalletsModel
                onCountChanged: root.showActiveViewTab()

                WalletView {
                    id: walletView
                    anchors.fill: parent
                    active: wallet === root.activeWallet
                    visible: active
                    settingsHost: root.walletSettingsHost
                    focusFallback: root
                    onTabRequested: function(tab) {
                        if (active) root.showWalletTab(tab)
                    }
                    onSettingsRequested: {
                        if (active) root.showSettingsSection("wallet")
                    }
                    onWalletSelectionRequested: root.openWalletSelection()

                    Connections {
                        target: walletView.wallet
                        ignoreUnknownSignals: true
                        function onAvailableChanged() {
                            Qt.callLater(root.syncOpenWallets)
                        }
                    }
                }
            }
        }

        // Node pages are shared by all wallets.
        StackLayout {
            anchors.fill: parent
            visible: !root.walletTabSelected
            currentIndex: navigationTabs.checkedButton ? Math.max(0, (navigationTabs.checkedButton as WalletNavigationTab).index - 3) : 0

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
                    renderingActive: root.visible && blockClockTabButton.checked
                }
            }
            PeersView {
                showHeader: false
                showBackButton: false
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
                        (item as SettingsView).selectSection(pendingRoute)
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
                        walletSettingsProvided: root.activeView !== null
                        activeWalletName: root.activeWallet ? root.activeWallet.name : ""
                        onSelectWalletRequested: root.openWalletSelection()
                        onWalletSettingsResetRequested: {
                            if (root.activeView) root.activeView.settingsStack.resetToRoot()
                        }
                    }
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
        busy: ApplicationModels.walletController.walletMigrationInProgress
        onConfirmed: {
            migrationRequiredPopup.errorText = ""
            migrationRequiredPopup.close()
            ApplicationModels.walletController.migrateWallet(root.pendingMigrationPath, "")
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
            ApplicationModels.walletController.migrateWallet(root.pendingMigrationPath, passphrase)
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
            onTriggered: ApplicationModels.walletController.closeWallet(closeConfirmationPopup.walletName)
        }
    }

}

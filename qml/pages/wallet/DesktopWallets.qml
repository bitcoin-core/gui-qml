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

WalletShellPage {
    id: root
    objectName: "desktopWalletsPage"
    background: null

    // The wallet shown in the window. Wallet pages never look this up
    // themselves; each WalletView gets its wallet when it is created.
    readonly property var activeWallet: ApplicationModels.walletController.isWalletLoaded
        && ApplicationModels.walletController.selectedWallet.available
        ? ApplicationModels.walletController.selectedWallet : null
    property int walletViewGeneration: 0
    readonly property WalletView activeView: {
        walletViews.count
        root.walletViewGeneration
        return root.viewForWallet(root.activeWallet)
    }
    readonly property bool walletTabSelected: navigationTabs.checkedButton !== null
        && (navigationTabs.checkedButton as WalletNavigationTab).index <= 2
    readonly property Item walletSettingsHost: settingsLoader.item ? (settingsLoader.item as SettingsView).walletSettingsHost : null
    readonly property SettingsView settingsView: settingsLoader.item as SettingsView
    modalNavigationBlocked: settingsView !== null
        && (settingsView.settingsNavigationPending
            || (settingsView.settingsEditor !== null && settingsView.settingsEditor.closeQuestion))
    readonly property alias walletViews: walletViews

    ButtonGroup { id: navigationTabs }

    onCloseWalletRequested: root.requestCloseWallet()
    onBackupRequested: root.startWalletBackup()
    onUriImportRequested: root.openUriImporter()
    onSignMessageRequested: root.openSignVerifyMessage(SignVerifyMessage.SignTab)
    onVerifyMessageRequested: root.openSignVerifyMessage(SignVerifyMessage.VerifyTab)
    onPsbtImportRequested: root.openPsbtImporter()
    onNodeRequested: root.openNode()
    onActivityRequested: root.openActivity()
    onSendRequested: root.openSend()
    onReceiveRequested: root.openReceive()
    onConsoleRequested: root.openConsole()
    onNetworkTrafficRequested: root.openNetworkTraffic()
    onPeersRequested: root.openPeers()
    onSettingsRequested: function(section) { root.openSettings(section) }
    onPresentWalletRequested: function(wallet) { root.openWallet(wallet as WalletQmlModel) }
    onContinueImportRequested: function(mailbox) { root.continueDesktopImport(mailbox) }

    function viewForWallet(wallet) {
        if (!wallet) return null
        const row = ApplicationModels.walletController.rowForWallet(wallet)
        return row >= 0 ? walletViews.itemAt(row) as WalletView : null
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

    function navigateToTransaction(txid, outputIndex) {
        if (root.activeView) root.activeView.navigateToTransaction(txid, outputIndex)
    }

    function requestCloseWallet() {
        if (!root.activeWallet) {
            return
        }
        ApplicationModels.walletController.requestCloseWallet(root.activeWallet)
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
        if (!wallet || wallet.available === false || ApplicationModels.walletController.rowForWallet(wallet) < 0) return
        if (ApplicationModels.walletController.selectedWallet !== wallet && !ApplicationModels.walletController.selectWallet(wallet)) return
        if (root.activeWallet === wallet) root.showWalletTab(WalletView.Activity)
    }

    function openActivity() {
        if (root.activeView) root.showWalletTab(WalletView.Activity)
    }

    function openSend() {
        if (root.activeView) root.showWalletTab(WalletView.Send)
    }

    function continueDesktopImport(mailbox: DesktopIntentModel): bool {
        const original = root.activeView
        if (!original || !original.wallet || !original.wallet.available) return false
        root.showWalletTab(WalletView.Send)
        if (root.activeView !== original || !original.active) return false
        return original.sendPage.acceptDesktopImport(mailbox)
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
        settingsLoader.applyPendingRoute()
    }

    Connections {
        target: ApplicationModels.walletController
        function onWalletLoadErrorNotice(name, operation, description) {
            // This bounded Native message belongs to this original operation;
            // later unrelated global errors cannot retarget the open popup.
            loadErrorPopup.walletName = name
            loadErrorPopup.operation = operation
            loadErrorPopup.preparedMessage = description
            loadErrorPopup.open()
        }
        function onWalletLoadWarningsOffered(operation) { migrationPresentations.offer(operation) }
        function onWalletMigrationRequired(path) { ApplicationModels.walletController.migrate(path) }
        function onMigrationOperationOffered(operation) { migrationPresentations.offer(operation) }

    }

    header: NavigationBar2 {
        id: navBar
        leftItem: WalletBadge {
            objectName: "walletBadge"
            checked: walletSelect.visible
            walletType: ApplicationModels.walletController.selectedWallet.keyScheme
            text: ApplicationModels.walletController.selectedWallet.displayName
            balance: ApplicationModels.walletController.selectedWallet.balance
            balanceKnown: ApplicationModels.walletController.selectedWallet.balanceKnown
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
                    ApplicationModels.walletController.requestCloseWallet(ApplicationModels.walletController.walletForName(name))
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
                textStyle: Theme.text.caption
                shorten: true
            }
            WalletNavigationTab {
                id: blockClockTabButton
                objectName: "blockClockTabButton"
                checked: true
                Layout.preferredWidth: 30
                index: 3
                ButtonGroup.group: navigationTabs
                Accessible.name: qsTr("Block clock")
                Accessible.description: blockClockTooltip.text
                customContent: MiniBlockClock {
                    pageSelected: blockClockTabButton.checked
                    selectedColor: blockClockTabButton.textActiveColor
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
                            qsTr("Blocktime\n%1").arg(Number(ApplicationModels.nodeModel.blockTipHeight).toLocaleString(Qt.locale(), 'f', 0))
                        } else if (connected && headerSyncActive) {
                            ApplicationModels.nodeModel.headerPresync ? qsTr("Pre-syncing headers") : qsTr("Syncing headers")
                        } else if (connected) {
                            qsTr("Downloading blocks\n%1").arg(syncState.text)
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
                Accessible.name: qsTr("Peers")
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
                Accessible.name: qsTr("Settings")

                Tooltip {
                    anchors.top: settingsTabButton.bottom
                    anchors.topMargin: 8
                    anchors.horizontalCenter: settingsTabButton.horizontalCenter
                    shown: settingsTabButton.hovered
                    text: qsTr("Settings")
                }
            }
        }
        background: Separator {
            anchors.bottom: navBar.bottom
            anchors.bottomMargin: 4
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
                model: ApplicationModels.walletController.openWalletModel
                onCountChanged: root.showActiveViewTab()
                onItemAdded: root.walletViewGeneration++
                onItemRemoved: root.walletViewGeneration++

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
                        const route = pendingRoute
                        pendingRoute = ""
                        if (settingsTabButton.checked) (item as SettingsView).selectSection(route)
                    }

                    // Create Settings on first use, then retain its navigation
                    // stacks while the parent tab item is hidden.
                    active: settingsTabButton.checked || retainItem
                    onLoaded: {
                        retainItem = true
                        applyPendingRoute()
                    }
                    sourceComponent: SettingsView {
                        showDoneButton: false
                        walletSettingsProvided: root.activeView !== null
                        activeWalletName: root.activeWallet ? root.activeWallet.name : ""
                        activeWalletRpcContext: root.activeWallet !== null
                        activeWalletRpcName: root.activeWallet ? root.activeWallet.rpcName : ""
                        activeWalletRpcReady: root.activeWallet !== null && root.activeWallet.capabilitiesKnown
                        activeWalletAvailable: root.activeWallet !== null && root.activeWallet.available
                        onSelectWalletRequested: root.openWalletSelection()
                        onWalletSettingsResetRequested: {
                            if (root.activeView) root.activeView.settingsStack.resetToRoot()
                        }
                    }
                }
            }
        }
    }

    WalletOperationPresentationModel { id: migrationPresentations }
    CoreText {
        anchors.bottom: parent.bottom
        width: parent.width
        text: migrationPresentations.error.message
        visible: text.length > 0
        wrapMode: Text.WordWrap
        color: Theme.color.dangerText
    }
    Repeater {
        model: migrationPresentations
        delegate: WalletOperationDialog {
            presenter: migrationPresentations
            maximumWidth: Math.min(420, root.width - 40)
        }
    }

    AlertPopup {
        id: loadErrorPopup
        objectName: "walletLoadErrorPopup"
        property string walletName: ""
        property double operation: 0
        property string preparedMessage: ""
        message: preparedMessage
        parent: Overlay.overlay
        title: qsTr("Failed to open wallet")
        messageObjectName: "walletLoadErrorPopupText"

        AlertAction {
            text: qsTr("OK")
            buttonObjectName: "walletLoadErrorPopupDismissButton"
        }
    }

    Repeater {
        objectName: "walletCloseQuestions"
        // One presentation instance per immutable opening/question pair.
        // Old buttons and native callbacks cannot resolve a newer target.
        model: {
            const wallet = ApplicationModels.walletController.closeTarget
            return wallet && wallet.closeQuestionToken.length > 0
                ? [{wallet: wallet, token: wallet.closeQuestionToken}] : []
        }
        delegate: Item {
            id: questionContext
            required property var modelData
            readonly property alias confirmation: closeConfirmationPopup
            AlertPopup {
                id: closeConfirmationPopup
                readonly property WalletQmlModel targetWallet: questionContext.modelData.wallet
                readonly property string questionToken: questionContext.modelData.token
                readonly property string walletName: targetWallet ? targetWallet.displayName : ""
                objectName: "walletCloseConfirmationPopup"
                parent: root.Overlay.overlay
                width: Math.min(420, root.width - 40)
                title: qsTr("Close wallet")
                visible: true
                message: qsTr("Do you want to close the wallet \"%1\"?").arg(walletName)
                onClosed: if (targetWallet) targetWallet.answerClose(questionToken, false)
                AlertAction {
                    text: qsTr("Cancel")
                    role: AlertAction.Cancel
                    buttonObjectName: "walletCloseConfirmationCancelButton"
                    onTriggered: if (closeConfirmationPopup.targetWallet) closeConfirmationPopup.targetWallet.answerClose(closeConfirmationPopup.questionToken, false)
                }
                AlertAction {
                    text: qsTr("Close wallet")
                    buttonObjectName: "walletCloseConfirmationConfirmButton"
                    // Answer before closing: onClosed answers an unanswered question with no.
                    closesPopup: false
                    onTriggered: {
                        if (closeConfirmationPopup.targetWallet) closeConfirmationPopup.targetWallet.answerClose(closeConfirmationPopup.questionToken, true)
                        closeConfirmationPopup.close()
                    }
                }
            }
        }
    }

}

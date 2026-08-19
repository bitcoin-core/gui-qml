// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0
import "../components"
import "../controls"
import "./node"
import "./wallet"

ApplicationWindow {
    id: appWindow
    objectName: "appWindow"
    title: qsTr("Bitcoin Core App")
    minimumWidth: 800
    minimumHeight: 665
    color: Theme.color.background
    palette.window: Theme.color.neutral1 // Context menu background
    palette.windowText: Theme.color.neutral8 // Menu item text and icons
    palette.dark: Theme.dark ? Theme.color.neutral2 : Theme.color.neutral3 // Menu border
    palette.mid: Theme.dark ? Theme.color.neutral2 : Theme.color.neutral3 // Menu separators
    palette.light: Theme.color.neutral3 // Highlighted menu item background
    palette.midlight: Theme.color.neutral3 // Pressed menu item background
    palette.disabled.windowText: Theme.color.neutral4 // Disabled item text and icons
    palette.disabled.light: Theme.color.neutral1 // Disabled item highlight background
    palette.disabled.midlight: Theme.color.neutral1 // Disabled item pressed background

    // The window starts hidden and is shown at the end of Component.onCompleted,
    // after the saved size has been applied (below). Applying the size while
    // hidden, before the first expose, lays the scene out exactly once at the
    // final size. That avoids the Qt 6.4.x Quick Layouts crash from resizing an
    // already-laid-out scene, and it avoids a live size binding pinned to
    // minimumWidth during construction (which loops popup layout on Qt 6.4).
    visible: false
    width: minimumWidth
    height: minimumHeight
    property bool walletAvailableForUi: AppMode.walletEnabled
    property bool appModeDesktopForUi: AppMode.isDesktop
    property bool preInitOnboardingRanForUi: false
    readonly property bool desktopWalletMode: walletAvailableForUi && appModeDesktopForUi
    readonly property bool waitForPostOnboardingWalletRoute: preInitOnboardingRanForUi && desktopWalletMode
    property bool postOnboardingWalletRouteResolved: false

    function resolvePostOnboardingWalletRoute() {
        if (appWindow.postOnboardingWalletRouteResolved) {
            return
        }
        if (!appWindow.waitForPostOnboardingWalletRoute) {
            return
        }
        if (!walletController.initialized || !walletListModel.walletDirLoaded) {
            return
        }
        appWindow.postOnboardingWalletRouteResolved = true
        main.replace(desktopWallets, {}, StackView.Immediate)
        if (walletController.noWalletsFound) {
            main.push(createWalletWizard, {
                "launchContext": CreateWalletWizard.Context.Onboarding
            }, StackView.Immediate)
        }
    }

    AppSettings {
        id: windowSettings
        property real windowX
        property real windowY
        property real windowWidth
        property real windowHeight
    }

    onXChanged: if (visibility === Window.Windowed) windowSettings.windowX = x
    onYChanged: if (visibility === Window.Windowed) windowSettings.windowY = y
    onWidthChanged: if (visibility === Window.Windowed) windowSettings.windowWidth = width
    onHeightChanged: if (visibility === Window.Windowed) windowSettings.windowHeight = height


    Behavior on color {
        ColorAnimation { duration: 150 }
    }

    // Tracks the previous visibility state to distinguish a user-initiated minimize
    // (Windowed → Minimized) from a WM restore transition (Hidden → Minimized).
    // The latter occurs when showNormal() calls setVisible(true) before
    // setWindowState(NoState); without this guard the onVisibilityChanged handler
    // would immediately re-hide the window, making tray-icon restore a no-op.
    property int m_prevVisibility: Window.AutomaticVisibility

    onClosing: (close) => {
        close.accepted = false
        if (desktopWindowBehaviorModel.shouldMinimizeWindowOnClose()) {
            showMinimized()
        } else {
            nodeModel.requestShutdown()
        }
    }

    onVisibilityChanged: function(visibility) {
        const prev = m_prevVisibility
        m_prevVisibility = visibility

        // Capture geometry once the window reaches its normal (Windowed) state.
        // The onWidthChanged/onHeightChanged writes only fire on a *change*, and
        // the size is set before the window is Windowed (the onCompleted self-
        // assign that breaks the size binding doesn't re-trigger them), so without
        // this the initial size is never saved (windowWidth/Height stay 0).
        if (visibility === Window.Windowed) {
            windowSettings.windowX = x
            windowSettings.windowY = y
            windowSettings.windowWidth = width
            windowSettings.windowHeight = height
        }

        if (visibility === Window.Minimized &&
                prev !== Window.Hidden &&
                desktopWindowBehaviorModel.shouldHideToTrayOnMinimize()) {
            desktopTrayIconController.hideMainWindow()
        }
    }

    Connections {
        target: desktopWindowBehaviorModel
        function onShowTrayIconChanged(show) {
            desktopTrayIconController.visible = AppMode.isDesktop && show
        }
    }

    Binding {
        target: desktopTrayIconController
        property: "windowVisible"
        value: appWindow.visible &&
               appWindow.visibility !== Window.Hidden &&
               appWindow.visibility !== Window.Minimized
    }

    Connections {
        target: desktopTrayIconController
        function onShowRequested() {
            desktopTrayIconController.showMainWindow()
        }
        function onHideRequested() {
            desktopTrayIconController.hideMainWindow()
        }
        function onQuitRequested() {
            nodeModel.requestShutdown()
        }
    }

    PageStack {
        id: main
        objectName: "mainPageStack"
        initialItem: appWindow.waitForPostOnboardingWalletRoute
            ? postOnboardingStartup
            : (appWindow.desktopWalletMode ? desktopWallets : node)
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

    NodeRuntimeDialog {
        parent: Overlay.overlay
    }

    NodeFatalErrorPopup {
        parent: Overlay.overlay
    }

    Connections {
        target: appWindow.desktopWalletMode ? walletController : null
        function onInitializedChanged() {
            appWindow.resolvePostOnboardingWalletRoute()
        }
        function onNoWalletsFoundChanged() {
            appWindow.resolvePostOnboardingWalletRoute()
        }
    }

    Connections {
        target: appWindow.desktopWalletMode ? walletListModel : null
        function onWalletDirLoadedChanged() {
            appWindow.resolvePostOnboardingWalletRoute()
        }
    }

    Component {
        id: postOnboardingStartup
        Page {
            objectName: "postOnboardingStartupPage"
            background: Rectangle {
                color: "black"
            }

            BusyIndicator {
                objectName: "postOnboardingStartupBusyIndicator"
                anchors.centerIn: parent
                running: true
            }
        }
    }

    Component {
        id: desktopWallets
        DesktopWallets {
            objectName: "desktopWalletsPage"
            onAddWallet: {
                main.push(createWalletWizard, { "launchContext": CreateWalletWizard.Context.Main })
            }
            onSendTransaction: {
                main.push(sendReviewPage)
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
            onTransactionSent: (txid) => {
                const externalSignerWallet = walletController.selectedWallet.hasExternalSigner
                const descriptionText = externalSignerWallet
                    ? qsTr("Approved on external signer. It should be confirmed within the next 10 minutes.")
                    : qsTr("Based on your selected fee, it should be confirmed within the next 10 minutes.")
                const actionText = externalSignerWallet ? qsTr("Done") : qsTr("Close window")
                walletController.selectedWallet.recipients.clear()
                main.push(sendResultPage, {
                    "descriptionText": descriptionText,
                    "actionText": actionText,
                    "txid": txid
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
            onViewNewTransaction: (txid) => {
                if (walletController.selectedWallet) {
                    walletController.selectedWallet.activityListModel.reload()
                }
                const walletPage = main.get(0)
                walletPage.navigateToTransaction(txid)
                main.pop(null)
            }
        }
    }

    Component {
        id: shutdown
        Shutdown {}
    }

    Component.onCompleted: {
        // Apply the saved geometry once, while the window is still hidden, then
        // show it. Sizing before the first expose lays the scene out a single
        // time at the final size: there is no live size binding to oscillate and
        // no resize of an already-laid-out scene (which crashes Qt 6.4 Quick
        // Layouts). Position is restored imperatively; moving a window does not
        // relayout its contents.
        if (windowSettings.windowWidth > 0) {
            width = windowSettings.windowWidth
        }
        if (windowSettings.windowHeight > 0) {
            height = windowSettings.windowHeight
        }
        if (windowSettings.windowWidth > 0 && windowSettings.windowHeight > 0) {
            x = windowSettings.windowX
            y = windowSettings.windowY
        }
        visible = true
        if (appWindow.desktopWalletMode) {
            nodeModel.startNodeInitializionThread()
            if (appWindow.waitForPostOnboardingWalletRoute) {
                Qt.callLater(appWindow.resolvePostOnboardingWalletRoute)
            }
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
                        nodeStack.push(settingsPage)
                    }
                    onPeersClicked: {
                        peerTableModel.startAutoRefresh()
                        nodeStack.push(peersPage)
                    }
                }
            }
            Component {
                id: settingsPage
                SettingsView {
                    onDoneClicked: nodeStack.pop()
                }
            }
            Component {
                id: peersPage
                Peers {
                    onBack: {
                        nodeStack.pop()
                        peerTableModel.stopAutoRefresh()
                    }
                    onPeerSelected: (peerDetails) => {
                        nodeStack.push(peerDetailsPage, {"details": peerDetails})
                    }
                    onBannedPeers: {
                        nodeStack.push(bannedPeersPage)
                    }
                }
            }
            Component {
                id: peerDetailsPage
                PeerDetails {
                    onBack: nodeStack.pop()
                }
            }
            Component {
                id: bannedPeersPage
                BannedPeers {
                    onBack: nodeStack.pop()
                }
            }
        }
    }
}

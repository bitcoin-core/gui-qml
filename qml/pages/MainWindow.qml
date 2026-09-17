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
    property bool nativeMenuAvailableForUi: true
    property bool preInitOnboardingRanForUi: false
    readonly property bool desktopWalletMode: walletAvailableForUi && appModeDesktopForUi
    readonly property bool waitForPostOnboardingWalletRoute: preInitOnboardingRanForUi && desktopWalletMode
    property bool postOnboardingWalletRouteResolved: false
    property bool shutdownInProgress: false
    readonly property var menuEditTarget: appWindow.editTarget(appWindow.activeFocusItem)
    readonly property var menuWalletController: appWindow.desktopWalletMode ? walletController : null
    readonly property bool menuNavigationEnabled: main.depth === 1
        && (!appWindow.waitForPostOnboardingWalletRoute || appWindow.postOnboardingWalletRouteResolved)

    function routeToShell(method, argument) {
        const shell = main.depth === 1 ? main.currentItem : null
        if (shell && typeof shell[method] === "function") {
            if (argument === undefined) {
                shell[method]()
            } else {
                shell[method](argument)
            }
        }
    }

    function editTarget(target) {
        if (!target) {
            return null
        }
        let item = target
        while (item) {
            if (item.visible === false) {
                return null
            }
            item = item.parent
        }
        return typeof target.undo === "function"
                || typeof target.redo === "function"
                || typeof target.copy === "function"
                || typeof target.paste === "function"
            ? target
            : null
    }

    function invokeEditCommand(method) {
        const target = appWindow.editTarget(appWindow.activeFocusItem)
        if (!target || typeof target[method] !== "function") {
            return
        }
        target[method]()
    }

    function openSettings(section) {
        appWindow.routeToShell("openSettings", section)
    }

    function openCreateWalletWizard() {
        if (!appWindow.menuNavigationEnabled) {
            return
        }
        main.push(createWalletWizard, {
            "launchContext": CreateWalletWizard.Context.Main
        })
    }

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

    Loader {
        active: appWindow.nativeMenuAvailableForUi
            && appWindow.appModeDesktopForUi
        sourceComponent: Component {
            Item {
                DesktopNativeMenuBar {
                    window: appWindow
                    actions: desktopMenuActions
                    active: true
                }
            }
        }
    }

    DesktopMenuActions {
        id: desktopMenuActions
        objectName: "desktopMenuActions"
        walletMode: appWindow.desktopWalletMode
        walletInitialized: appWindow.menuWalletController
            ? appWindow.menuWalletController.initialized
            : false
        walletLoaded: appWindow.menuWalletController
            ? appWindow.menuWalletController.isWalletLoaded
                && appWindow.menuWalletController.selectedWallet !== null
            : false
        walletBusy: appWindow.menuWalletController
            ? appWindow.menuWalletController.walletLoadInProgress
            : false
        canUndo: appWindow.menuEditTarget
            && typeof appWindow.menuEditTarget.undo === "function"
            && appWindow.menuEditTarget.canUndo === true
        canRedo: appWindow.menuEditTarget
            && typeof appWindow.menuEditTarget.redo === "function"
            && appWindow.menuEditTarget.canRedo === true
        canCopy: appWindow.menuEditTarget
            && typeof appWindow.menuEditTarget.copy === "function"
            && appWindow.menuEditTarget.selectedText
            && appWindow.menuEditTarget.selectedText.length > 0
        canPaste: appWindow.menuEditTarget
            && typeof appWindow.menuEditTarget.paste === "function"
            && appWindow.menuEditTarget.readOnly !== true
            && appWindow.menuEditTarget.enabled !== false
        navigationEnabled: appWindow.menuNavigationEnabled
        shuttingDown: appWindow.shutdownInProgress
        isMacOs: Qt.platform.os === "osx"

        onCreateWalletRequested: appWindow.openCreateWalletWizard()
        onCloseWalletRequested: appWindow.routeToShell("requestCloseWallet")
        onBackupWalletRequested: appWindow.routeToShell("startWalletBackup")
        onOpenUriRequested: appWindow.routeToShell("openUriImporter")
        onSignMessageRequested: appWindow.routeToShell("openSignVerifyMessage", SignVerifyMessage.SignTab)
        onVerifyMessageRequested: appWindow.routeToShell("openSignVerifyMessage", SignVerifyMessage.VerifyTab)
        onLoadPsbtRequested: appWindow.routeToShell("openPsbtImporter")
        onExitRequested: nodeModel.requestShutdown()
        onSettingsRequested: appWindow.openSettings("display")
        onUndoRequested: appWindow.invokeEditCommand("undo")
        onRedoRequested: appWindow.invokeEditCommand("redo")
        onCopyRequested: appWindow.invokeEditCommand("copy")
        onPasteRequested: appWindow.invokeEditCommand("paste")
        onMinimizeRequested: appWindow.showMinimized()
        onZoomRequested: {
            if (appWindow.visibility === Window.Maximized) {
                appWindow.showNormal()
            } else {
                appWindow.showMaximized()
            }
        }
        onMainWindowRequested: desktopTrayIconController.showMainWindow()
        onNodeRequested: appWindow.routeToShell("openNode")
        onActivityRequested: appWindow.routeToShell("openActivity")
        onSendRequested: appWindow.routeToShell("openSend")
        onReceiveRequested: appWindow.routeToShell("openReceive")
        onInformationRequested: menuInformationPopup.open()
        onConsoleRequested: appWindow.routeToShell("openConsole")
        onNetworkTrafficRequested: appWindow.routeToShell("openNetworkTraffic")
        onPeersRequested: appWindow.routeToShell("openPeers")
        onRpcDocumentationRequested: Qt.openUrlExternally("https://bitcoincore.org/en/doc/")
        onAboutRequested: appWindow.openSettings("about")
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
            appWindow.shutdownInProgress = true
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

    NodeInformationPopup {
        id: menuInformationPopup
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

            function resetToRoot() {
                peerTableModel.stopAutoRefresh()
                if (nodeStack.depth > 1) {
                    nodeStack.pop(null, StackView.Immediate)
                }
            }

            function openSettings(section) {
                nodeStack.resetToRoot()
                const page = nodeStack.push(settingsPage, {}, StackView.Immediate)
                if (section && page) {
                    page.selectSection(section)
                }
            }

            function openNode() {
                nodeStack.resetToRoot()
            }

            function openPeers() {
                nodeStack.resetToRoot()
                peerTableModel.startAutoRefresh()
                nodeStack.push(peersPage, {}, StackView.Immediate)
            }

            function openConsole() {
                nodeStack.openSettings("rpc-console")
            }

            function openNetworkTraffic() {
                nodeStack.openSettings("network-traffic")
            }
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
                PeersView {
                    onBack: {
                        nodeStack.pop()
                        peerTableModel.stopAutoRefresh()
                    }
                }
            }
        }
    }
}

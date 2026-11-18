// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQml.Models 2.15
import org.bitcoincore.qt 1.0
import "../components"
import "../controls"
import "./node"

ApplicationWindow {
    id: appWindow
    objectName: "appWindow"
    title: qsTr("Bitcoin Core App")
    minimumWidth: SizeClass.windowMinimumWidth
    minimumHeight: 665
    color: Theme.color.background
    palette.window: Theme.color.surface1 // Context menu background
    palette.windowText: Theme.color.neutral8 // Menu item text and icons
    palette.dark: Theme.color.menuBorder // Menu border
    palette.mid: Theme.color.menuDivider // Menu separators
    palette.light: Theme.color.surface3 // Highlighted menu item background
    palette.midlight: Theme.color.surface3 // Pressed menu item background
    palette.disabled.windowText: Theme.color.disabledContent // Disabled item text and icons
    palette.disabled.light: Theme.color.surface1 // Disabled item highlight background
    palette.disabled.midlight: Theme.color.surface1 // Disabled item pressed background
    palette.highlight: Theme.color.selection // Text selection in fields
    palette.highlightedText: Theme.color.selectedText

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
    property bool onboardingWalletPresentationPending: false
    property bool shutdownInProgress: false
    footer: DesktopRequestsBar {
        mailbox: ApplicationModels.desktopIntentModel
        visible: !appWindow.shutdownInProgress && mailbox !== null
            && (mailbox.pendingCount > 0 || mailbox.hasInspection || mailbox.error.length > 0)
        enabled: appWindow.menuNavigationEnabled
        continuationAvailable: appWindow.desktopWalletMode && appWindow.menuNavigationEnabled
            && appWindow.menuWalletController !== null && appWindow.menuWalletController.selectedWallet !== null
            && appWindow.menuWalletController.selectedWallet.available
        onContinueRequested: {
            if (!continuationAvailable) return
            const shell = appWindow.desktopShell
            if (shell) shell.continueImportRequested(mailbox)
        }
    }
    readonly property var menuEditTarget: appWindow.editTarget(appWindow.activeFocusItem)
    readonly property var menuWalletController: appWindow.desktopWalletMode ? ApplicationModels.walletController : null
    readonly property WalletShellPage desktopShell: main.currentItem as WalletShellPage
    readonly property WalletCreationPort walletCreationHost: walletCreationModal.item as WalletCreationPort
    readonly property NodeShell nodeShell: main.currentItem as NodeShell
    // Native menus do not participate in Qt Quick's popup pointer grab. The
    // same bounded presentation gate also rejects a delayed direct route.
    readonly property bool modalNavigationBlocked: (appWindow.walletCreationHost !== null
            && appWindow.walletCreationHost.modalVisible)
        || ApplicationModels.nodeModel.runtimeDialogVisible
        || (appWindow.desktopShell !== null && appWindow.desktopShell.modalNavigationBlocked)
        || (appWindow.nodeShell !== null && appWindow.nodeShell.modalNavigationBlocked)
        || (appWindow.menuWalletController !== null
            && appWindow.menuWalletController.closeTarget !== null
            && appWindow.menuWalletController.closeTarget.closeQuestion !== null)
    readonly property bool menuNavigationEnabled: main.depth === 1
        && (!appWindow.waitForPostOnboardingWalletRoute || appWindow.postOnboardingWalletRouteResolved)
        && !appWindow.modalNavigationBlocked
        && !appWindow.shutdownInProgress && !ApplicationModels.nodeModel.fatalException
    readonly property bool onboardingWalletPresentationAdmitted: appWindow.onboardingWalletPresentationPending
        && appWindow.desktopWalletMode && appWindow.menuNavigationEnabled
        && walletCreationModal.item !== null && ApplicationModels.walletController.noWalletsFound

    onDesktopWalletModeChanged: {
        if (!appWindow.desktopWalletMode) appWindow.onboardingWalletPresentationPending = false
    }

    enum NavigationRoute { RouteCloseWallet, RouteBackup, RouteImportUri, RouteSignMessage,
        RouteVerifyMessage, RouteImportPsbt, RouteNode, RouteActivity, RouteSend,
        RouteReceive, RouteConsole, RouteNetworkTraffic, RoutePeers }

    function routeToShell(route: int) {
        if (!appWindow.menuNavigationEnabled) return
        const desktop = appWindow.desktopShell
        if (desktop !== null) {
            switch (route) {
            case MainWindow.RouteCloseWallet: desktop.closeWalletRequested(); break
            case MainWindow.RouteBackup: desktop.backupRequested(); break
            case MainWindow.RouteImportUri: desktop.uriImportRequested(); break
            case MainWindow.RouteSignMessage: desktop.signMessageRequested(); break
            case MainWindow.RouteVerifyMessage: desktop.verifyMessageRequested(); break
            case MainWindow.RouteImportPsbt: desktop.psbtImportRequested(); break
            case MainWindow.RouteNode: desktop.nodeRequested(); break
            case MainWindow.RouteActivity: desktop.activityRequested(); break
            case MainWindow.RouteSend: desktop.sendRequested(); break
            case MainWindow.RouteReceive: desktop.receiveRequested(); break
            case MainWindow.RouteConsole: desktop.consoleRequested(); break
            case MainWindow.RouteNetworkTraffic: desktop.networkTrafficRequested(); break
            case MainWindow.RoutePeers: desktop.peersRequested(); break
            }
            return
        }
        const node = appWindow.nodeShell
        if (node === null) return
        switch (route) {
        case MainWindow.RouteNode: node.openNode(); break
        case MainWindow.RouteConsole: node.openConsole(); break
        case MainWindow.RouteNetworkTraffic: node.openNetworkTraffic(); break
        case MainWindow.RoutePeers: node.openPeers(); break
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

    function openSettings(section: string) {
        if (!appWindow.menuNavigationEnabled) return
        if (appWindow.desktopShell !== null) appWindow.desktopShell.settingsRequested(section)
        else if (appWindow.nodeShell !== null) appWindow.nodeShell.openSettings(section)
    }

    function openWalletCreation() {
        if (!appWindow.desktopWalletMode || !appWindow.menuNavigationEnabled) {
            return
        }
        appWindow.onboardingWalletPresentationPending = false
        walletCreationModal.open()
    }

    // This consumes the exact retained operation result. Popup exit animation
    // is presentation only and cannot turn its completion into a fresh route.
    function presentCreatedWallet(wallet: QtObject) {
        if (appWindow.shutdownInProgress || ApplicationModels.nodeModel.fatalException) return
        const shell = main.depth === 1 ? appWindow.desktopShell : null
        if (shell !== null) shell.presentWalletRequested(wallet)
    }

    function resolvePostOnboardingWalletRoute() {
        if (appWindow.postOnboardingWalletRouteResolved) {
            return
        }
        if (!appWindow.waitForPostOnboardingWalletRoute) {
            return
        }
        if (!ApplicationModels.walletController.initialized || !ApplicationModels.walletListModel.walletDirLoaded) {
            return
        }
        appWindow.postOnboardingWalletRouteResolved = true
        if (ApplicationModels.walletController.noWalletsFound) {
            appWindow.onboardingWalletPresentationPending = true
        }
    }

    Timer {
        id: onboardingWalletResolution
        interval: 0
        repeat: false
        onTriggered: appWindow.resolvePostOnboardingWalletRoute()
    }

    Timer {
        id: onboardingWalletPresentation
        interval: 0
        repeat: false
        running: appWindow.onboardingWalletPresentationAdmitted
        onTriggered: {
            if (!appWindow.onboardingWalletPresentationAdmitted) return
            appWindow.routeToShell(MainWindow.RouteNode)
            // Navigation can synchronously publish another owned modal or
            // stopping state. Retain this presentation until it is admitted.
            if (!appWindow.onboardingWalletPresentationAdmitted) return
            appWindow.onboardingWalletPresentationPending = false
            walletCreationModal.openForOnboarding()
        }
    }

    property bool preferenceGeometryRestored: false
    function restorePreferenceGeometry() {
        if (preferenceGeometryRestored || !windowSettings.ready) return
        windowSettings.restoreWindow(appWindow)
        preferenceGeometryRestored = true
        if (visibility === Window.Windowed) windowSettings.requestGeometry(x, y, width, height)
    }
    AppPreferences { id: windowSettings; onChanged: appWindow.restorePreferenceGeometry() }
    // Geometry is a bounded native GUI handoff; storage is owned by the shared
    // background store and always captures the original full rectangle.
    onActiveChanged: if (active) {
        windowSettings.refresh()
        ApplicationModels.optionsModel.refreshDisplaySettings()
        ApplicationModels.desktopWindowBehaviorModel.refreshSettings()
    }
    onXChanged: if (visibility === Window.Windowed && preferenceGeometryRestored && windowSettings.ready) windowSettings.requestGeometry(x, y, width, height)
    onYChanged: if (visibility === Window.Windowed && preferenceGeometryRestored && windowSettings.ready) windowSettings.requestGeometry(x, y, width, height)
    onWidthChanged: if (visibility === Window.Windowed && preferenceGeometryRestored && windowSettings.ready) windowSettings.requestGeometry(x, y, width, height)
    onHeightChanged: if (visibility === Window.Windowed && preferenceGeometryRestored && windowSettings.ready) windowSettings.requestGeometry(x, y, width, height)
    Popup {
        parent: Overlay.overlay
        anchors.centerIn: parent
        visible: windowSettings.error.length > 0
        closePolicy: Popup.NoAutoClose
        Column {
            spacing: 12
            CoreText { text: windowSettings.error; wrapMode: Text.WordWrap; width: 360 }
            Button { text: qsTr("Retry preferences"); enabled: !windowSettings.pending; onClicked: windowSettings.retry() }
        }
    }


    Behavior on color {
        enabled: Theme.motion.animated
        ColorAnimation { duration: Theme.motion.normal }
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
        shuttingDown: appWindow.shutdownInProgress || ApplicationModels.nodeModel.fatalException
        fatalException: ApplicationModels.nodeModel.fatalException
        isMacOs: Qt.platform.os === "osx"
        amountsHidden: ApplicationModels.optionsModel.hideAmounts

        onCreateWalletRequested: appWindow.openWalletCreation()
        onCloseWalletRequested: appWindow.routeToShell(MainWindow.RouteCloseWallet)
        onBackupWalletRequested: appWindow.routeToShell(MainWindow.RouteBackup)
        onOpenUriRequested: appWindow.routeToShell(MainWindow.RouteImportUri)
        onSignMessageRequested: appWindow.routeToShell(MainWindow.RouteSignMessage)
        onVerifyMessageRequested: appWindow.routeToShell(MainWindow.RouteVerifyMessage)
        onLoadPsbtRequested: appWindow.routeToShell(MainWindow.RouteImportPsbt)
        onExitRequested: ApplicationModels.nodeModel.requestShutdown()
        onSettingsRequested: appWindow.openSettings("display")
        onUndoRequested: appWindow.invokeEditCommand("undo")
        onRedoRequested: appWindow.invokeEditCommand("redo")
        onCopyRequested: appWindow.invokeEditCommand("copy")
        onPasteRequested: appWindow.invokeEditCommand("paste")
        onToggleDisplayUnitRequested: ApplicationModels.optionsModel.displayUnit = ApplicationModels.optionsModel.displayUnit === BitcoinAmount.SAT
            ? BitcoinAmount.BTC : BitcoinAmount.SAT
        onToggleHideAmountsRequested: ApplicationModels.optionsModel.hideAmounts = !ApplicationModels.optionsModel.hideAmounts
        onMinimizeRequested: appWindow.showMinimized()
        onZoomRequested: {
            if (appWindow.visibility === Window.Maximized) {
                appWindow.showNormal()
            } else {
                appWindow.showMaximized()
            }
        }
        onMainWindowRequested: ApplicationModels.desktopTrayIconController.showMainWindow()
        onNodeRequested: appWindow.routeToShell(MainWindow.RouteNode)
        onActivityRequested: appWindow.routeToShell(MainWindow.RouteActivity)
        onSendRequested: appWindow.routeToShell(MainWindow.RouteSend)
        onReceiveRequested: appWindow.routeToShell(MainWindow.RouteReceive)
        onInformationRequested: appWindow.openNodeInformation()
        onConsoleRequested: appWindow.routeToShell(MainWindow.RouteConsole)
        onNetworkTrafficRequested: appWindow.routeToShell(MainWindow.RouteNetworkTraffic)
        onPeersRequested: appWindow.routeToShell(MainWindow.RoutePeers)
        onRpcDocumentationRequested: rpcDocumentationLinkPopup.openLink("https://bitcoincore.org/en/doc/")
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
        if (!ApplicationModels.nodeModel.fatalException && ApplicationModels.desktopWindowBehaviorModel.shouldMinimizeWindowOnClose()) {
            showMinimized()
        } else {
            ApplicationModels.nodeModel.requestShutdown()
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
            if (windowSettings.ready) windowSettings.requestGeometry(x, y, width, height)
        }

        if (visibility === Window.Minimized &&
                prev !== Window.Hidden &&
                ApplicationModels.desktopWindowBehaviorModel.shouldHideToTrayOnMinimize()) {
            ApplicationModels.desktopTrayIconController.hideMainWindow()
        }
    }

    Connections {
        target: ApplicationModels.desktopWindowBehaviorModel
        function onShowTrayIconChanged(show) {
            ApplicationModels.desktopTrayIconController.visible = AppMode.isDesktop && show
        }
    }

    Binding {
        target: ApplicationModels.desktopTrayIconController
        property: "windowVisible"
        value: appWindow.visible &&
               appWindow.visibility !== Window.Hidden &&
               appWindow.visibility !== Window.Minimized
    }

    Connections {
        target: ApplicationModels.desktopTrayIconController
        function onShowRequested() {
            ApplicationModels.desktopTrayIconController.showMainWindow()
        }
        function onHideRequested() {
            ApplicationModels.desktopTrayIconController.hideMainWindow()
        }
        function onQuitRequested() {
            ApplicationModels.nodeModel.requestShutdown()
        }
    }

    PageStack {
        id: main
        objectName: "mainPageStack"
        enabled: !appWindow.shutdownInProgress && !ApplicationModels.nodeModel.fatalException
        visible: !appWindow.shutdownInProgress
        initialItem: appWindow.desktopWalletMode ? "qrc:/qml/pages/wallet/DesktopWallets.qml" : nodeShellComponent
        anchors.fill: parent
        focus: true
        Keys.onReleased: (event) => {
            if (event.key == Qt.Key_Back) {
                ApplicationModels.nodeModel.requestShutdown()
                event.accepted = true
            }
        }
    }

    Connections {
        target: ApplicationModels.nodeModel
        function onFatalExceptionChanged() {
            if (!ApplicationModels.nodeModel.fatalException) return
            appWindow.onboardingWalletPresentationPending = false
            appWindow.showNormal()
            appWindow.raise()
            appWindow.requestActivate()
        }
        function onRequestedShutdown() {
            appWindow.onboardingWalletPresentationPending = false
            appWindow.shutdownInProgress = true
        }
    }

    Instantiator {
        model: ApplicationModels.nodeModel.runtimePrompts
        delegate: NodeRuntimeDialog {
            required property RuntimePromptModel modelData
            prompt: modelData
            parent: appWindow.Overlay.overlay
        }
    }

    NodeFatalErrorPopup {
        parent: Overlay.overlay
    }

    // One of each for the whole window: the menu bar and every
    // NodeStatusActions open these.
    NodeInformationPopup {
        id: nodeInformationPopup
        parent: Overlay.overlay
    }

    NodeWarningsPopup {
        id: nodeWarningsPopup
        parent: Overlay.overlay
    }

    ExternalPopup {
        id: rpcDocumentationLinkPopup
        objectName: "rpcDocumentationLinkPopup"
        parent: Overlay.overlay
    }

    function openNodeInformation() {
        nodeInformationPopup.open()
    }

    function openNodeWarnings() {
        nodeWarningsPopup.open()
    }

    Connections {
        target: appWindow.desktopWalletMode ? ApplicationModels.walletController : null
        function onInitializedChanged() {
            appWindow.resolvePostOnboardingWalletRoute()
        }
        function onNoWalletsFoundChanged() {
            if (!ApplicationModels.walletController.noWalletsFound) appWindow.onboardingWalletPresentationPending = false
            appWindow.resolvePostOnboardingWalletRoute()
        }
    }

    Connections {
        target: appWindow.desktopWalletMode ? ApplicationModels.walletListModel : null
        function onWalletDirLoadedChanged() {
            appWindow.resolvePostOnboardingWalletRoute()
        }
    }

    Connections {
        target: appWindow.desktopWalletMode && !appWindow.shutdownInProgress && main.depth === 1
            ? main.currentItem : null
        function onAddWallet() { appWindow.openWalletCreation() }
    }

    Loader {
        id: walletCreationModal
        active: appWindow.desktopWalletMode
        source: active ? "../components/WalletCreationHost.qml" : ""
        function open() { if (item) (item as WalletCreationPort).openRequested() }
        function openForOnboarding() { if (item) (item as WalletCreationPort).onboardingOpenRequested() }
    }

    Connections {
        target: walletCreationModal.item
        function onFinished(openActivity, wallet) {
            if (openActivity) appWindow.presentCreatedWallet(wallet)
        }
    }

    Component {
        id: shutdown
        Shutdown {}
    }

    // Keep page/delegate contexts alive until the scene is torn down after
    // workers drain. Clearing a live Activity ListView during shutdown can
    // invalidate Qt's pending section/delegate changes (upstream #1116).
    Loader {
        anchors.fill: parent
        active: appWindow.shutdownInProgress
        sourceComponent: shutdown
    }

    Component.onCompleted: {
        // Apply the saved geometry once, while the window is still hidden, then
        // show it. Sizing before the first expose lays the scene out a single
        // time at the final size: there is no live size binding to oscillate and
        // no resize of an already-laid-out scene (which crashes Qt 6.4 Quick
        // Layouts). Position is restored imperatively; moving a window does not
        // relayout its contents.
        restorePreferenceGeometry()
        visible = true
        if (appWindow.desktopWalletMode) {
            ApplicationModels.nodeModel.startNodeInitializionThread()
            if (appWindow.waitForPostOnboardingWalletRoute) {
                onboardingWalletResolution.restart()
            }
        }
    }

    Component {
        id: nodeShellComponent
        NodeShell {
            id: nodeShellItem
            navigationAdmitted: appWindow.nodeShell === nodeShellItem && appWindow.menuNavigationEnabled
            stopping: appWindow.shutdownInProgress || ApplicationModels.nodeModel.fatalException
            settingsComponent: SettingsView {
                wallet: typeof walletController !== "undefined" && ApplicationModels.walletController.isWalletLoaded
                    ? ApplicationModels.walletController.selectedWallet : null
                activeWalletName: wallet ? wallet.name : ""
                onDoneClicked: nodeShellItem.closeSettings()
            }
            onSettingsRequested: (section) => appWindow.openSettings(section)
            onPeersRequested: appWindow.routeToShell(MainWindow.RoutePeers)
        }
    }
}

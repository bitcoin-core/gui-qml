// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import org.bitcoincore.qt 1.0

import "../../controls"

// The pages of one wallet: Activity, Send, Receive and wallet settings, each
// with its own navigation stack. The wallet never changes for the lifetime of
// a view, so pages and dialogs inside it always act on this wallet.
Item {
    id: root
    objectName: "walletView"

    enum Tab {
        Activity,
        Send,
        Receive
    }

    required property WalletQmlModel wallet
    property int currentTab: WalletView.Activity
    // True while this is the wallet shown in the window.
    property bool active: false
    // Where the wallet settings stack is shown while this view is active. The
    // stack returns to the view when the host goes away or the view is hidden.
    property Item settingsHost: null
    // Takes keyboard focus away from a page of this view when it is hidden.
    property Item focusFallback: null

    readonly property alias activityPage: activityPage
    readonly property alias sendPage: sendPage
    readonly property alias receivePage: receivePage
    readonly property alias settingsStack: settingsStack

    // The shell shows a wallet tab or the wallet settings for these requests
    // when this view is active.
    signal tabRequested(int tab)
    signal settingsRequested()
    signal walletSelectionRequested()

    function requestTab(tab) {
        root.currentTab = tab
        root.tabRequested(tab)
    }

    function navigateToTransaction(txid, outputIndex) {
        root.requestTab(WalletView.Activity)
        activityPage.navigateToTransaction(txid, outputIndex)
    }

    function openPaymentRequest(requestId) {
        root.requestTab(WalletView.Activity)
        activityPage.openPaymentRequest(requestId)
    }

    function openUriImporter() {
        root.requestTab(WalletView.Send)
        sendPage.openPaymentRequestImport()
    }

    function openPsbtImporter() {
        root.requestTab(WalletView.Send)
        sendPage.openPsbtFileImport()
    }

    function openSettingsRoute(route, argument) {
        if (route === "addresses") {
            settingsStack.openAddressHistory()
        } else if (route === "password") {
            settingsStack.openPassword(argument)
        } else if (route === "sign-verify-message") {
            settingsStack.openSignVerifyMessage(argument)
        } else if (route === "backup") {
            settingsStack.resetToRoot()
        }
        root.settingsRequested()
        // The backup dialog belongs to the settings page, so start it once
        // the page is shown, unless this wallet was hidden in the meantime.
        if (route === "backup") {
            const hiddenCount = root.hiddenCount
            Qt.callLater(function() {
                if (root.active && hiddenCount === root.hiddenCount) settingsStack.startBackup()
            })
        }
    }

    // Counts how often this view or one of its pages stopped being shown,
    // so deferred opens can tell they are stale.
    property int hiddenCount: 0

    // Popups and menus are not closed when the item that declares them is
    // hidden; popups parented to Overlay.overlay are not even hidden with it.
    // Native file dialogs are separate windows. So each page closes what it
    // owns when it is hidden: when another wallet is shown, when the window
    // shows node pages or settings, or when the wallet settings section is
    // left. Nothing from one wallet stays on screen over other pages.
    // Hiding an item does not take keyboard focus from it either, so typing
    // after a switch would still edit the hidden page.
    function pageHidden(page) {
        root.hiddenCount++
        root.closePopupsIn(page)
        root.releaseFocusFrom(page)
    }

    function releaseFocusFrom(page) {
        const focused = root.Window.activeFocusItem
        let item = focused
        while (item && item !== page) item = item.parent
        if (!item) return
        // Clear the whole focus chain inside the page, so focusing the
        // fallback cannot lead back into it through a focus scope.
        for (item = focused; item && item !== page.parent; item = item.parent) item.focus = false
        if (root.focusFallback) root.focusFallback.forceActiveFocus()
    }

    function closeOwnedPopups() {
        root.hiddenCount++
        root.closePopupsIn(root)
        if (settingsStack.parent !== settingsHome) root.closePopupsIn(settingsStack)
    }

    function closePopupsIn(object) {
        if (!object) return
        if (typeof object.close === "function") {
            if (object.closePolicy !== undefined) {
                if (object.visible) object.close()
                root.closePopupsIn(object.contentItem)
                return
            }
            if (object.fileMode !== undefined) {
                if (object.visible) object.close()
                return
            }
        }
        const children = object.children
        if (children) {
            for (let index = 0; index < children.length; ++index) root.closePopupsIn(children[index])
        }
        const resources = object.resources
        if (resources) {
            for (let index = 0; index < resources.length; ++index) root.closePopupsIn(resources[index])
        }
    }

    onActiveChanged: if (!active) root.closeOwnedPopups()
    Component.onDestruction: root.closeOwnedPopups()

    Connections {
        target: walletController
        function onOpenReceiveRequested(wallet) {
            if (wallet === root.wallet) root.requestTab(WalletView.Receive)
        }
        function onOpenWalletSettingsRequested(wallet) {
            if (wallet === root.wallet) root.openSettingsRoute("wallet")
        }
    }

    StackLayout {
        id: tabs
        objectName: "walletViewTabs"
        anchors.fill: parent
        currentIndex: root.currentTab

        ActivityList {
            id: activityPage
            wallet: root.wallet
            onVisibleChanged: if (!visible) root.pageHidden(activityPage)
        }
        SendCreate {
            id: sendPage
            wallet: root.wallet
            onVisibleChanged: if (!visible) root.pageHidden(sendPage)
            onViewTransactionInActivity: (txid) => root.navigateToTransaction(txid)
        }
        ReceivePage {
            id: receivePage
            wallet: root.wallet
            onVisibleChanged: if (!visible) root.pageHidden(receivePage)
            onPaymentRequestCreated: {
                activityPage.pop(null)
                root.requestTab(WalletView.Activity)
            }
            onAddressHistoryRequested: root.openSettingsRoute("addresses")
        }
    }

    Item {
        id: settingsHome
        anchors.fill: parent
        visible: false
    }

    WalletSettingsStack {
        id: settingsStack
        parent: root.settingsHost && root.active ? root.settingsHost : settingsHome
        anchors.fill: parent
        wallet: root.wallet
        onVisibleChanged: if (!visible) root.pageHidden(settingsStack)
        onSelectWalletRequested: root.walletSelectionRequested()
        onReceiveRequested: root.requestTab(WalletView.Receive)
        onPaymentRequestRequested: function(requestId) { root.openPaymentRequest(requestId) }
    }
}

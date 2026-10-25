// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import org.bitcoincore.qt 1.0

import "../controls"
import "../pages/settings" as SettingsPages
import "../pages/wallet" as WalletPages

Page {
    id: root
    objectName: "settingsView"

    signal doneClicked()
    signal selectWalletRequested()
    signal receiveRequested()
    signal paymentRequestRequested(string requestId)

    property bool showDoneButton: true
    property string selectedSectionId: ""
    property int sidebarWidth: 286
    // Compact widths show one column: the sidebar is the root list, and
    // choosing a section slides its page over it until Back.
    readonly property bool compact: splitView.isCompact
    readonly property SettingsSidebar sidebar: splitView.primaryItem ? splitView.primaryItem.sidebar : null
    readonly property SettingsPageContainer pageContainer: splitView.detailItem
    readonly property var groupTitles: ({
        "wallet": qsTr("Wallet"),
        "general": qsTr("General"),
        "network": qsTr("Network"),
        "advanced": qsTr("Advanced")
    })
    readonly property var sections: [
        {
            id: "wallet",
            label: qsTr("Wallet settings"),
            group: "wallet",
            visible: AppMode.walletEnabled,
            pageComponent: walletPage
        },
        {
            id: "external-signer",
            label: qsTr("External signer"),
            group: "wallet",
            visible: AppMode.walletEnabled,
            pageComponent: externalSignerPage
        },
        {
            id: "display",
            label: qsTr("Display"),
            group: "general",
            pageComponent: displayPage
        },
        {
            id: "window-behavior",
            label: qsTr("Window behavior"),
            group: "general",
            visible: AppMode.isDesktop,
            pageComponent: windowBehaviorPage
        },
        {
            id: "storage",
            label: qsTr("Storage"),
            group: "general",
            pageComponent: storagePage
        },
        {
            id: "connection",
            label: qsTr("Connection"),
            group: "network",
            pageComponent: connectionPage
        },
        {
            id: "network-traffic",
            label: qsTr("Network traffic"),
            group: "network",
            pageComponent: networkTrafficPage
        },
        {
            id: "mempool",
            label: qsTr("Mempool information"),
            group: "advanced",
            visible: nodeModel.mempoolInformationAvailable,
            pageComponent: mempoolPage
        },
        {
            id: "rpc-console",
            label: qsTr("RPC console"),
            group: "advanced",
            visible: AppMode.isDesktop,
            pageComponent: rpcConsolePage
        },
        {
            id: "debug-log",
            label: qsTr("Debug log"),
            group: "advanced",
            pageComponent: debugLogPage
        },
        {
            id: "about",
            label: qsTr("About"),
            group: "about",
            pageComponent: aboutPage
        }
    ]

    function sectionForId(sectionId) {
        for (let index = 0; index < root.sections.length; ++index) {
            if (root.sections[index].id === sectionId) return root.sections[index]
        }
        return null
    }

    function componentForSection(sectionId) {
        const section = root.sectionForId(sectionId)
        return section ? section.pageComponent : null
    }

    function sectionIsVisible(sectionId) {
        const section = root.sectionForId(sectionId)
        return section !== null && section.visible !== false
    }

    function firstVisibleSectionId() {
        for (let index = 0; index < root.sections.length; ++index) {
            if (root.sections[index].visible !== false) return root.sections[index].id
        }
        return ""
    }

    function selectSection(sectionId, forceReload) {
        if (!root.pageContainer) return
        const resolvedId = root.sectionIsVisible(sectionId) ? sectionId : root.firstVisibleSectionId()
        if (resolvedId.length === 0) {
            root.selectedSectionId = ""
            root.pageContainer.clear()
            return
        }

        if (forceReload === true) root.pageContainer.clear()
        root.selectedSectionId = resolvedId
        root.pageContainer.showSection(resolvedId, root.componentForSection(resolvedId))
    }

    function showSectionPage(sectionId, forceReload) {
        root.selectSection(sectionId, forceReload)
        if (root.selectedSectionId.length > 0) splitView.showDetail()
    }

    function ensureVisibleSelection() {
        if (!root.sectionIsVisible(root.selectedSectionId)) root.selectSection(root.firstVisibleSectionId())
    }

    function openWalletSettings() {
        root.showSectionPage("wallet")
    }

    function openWalletAddressHistory() {
        if (!walletController.isWalletLoaded || !walletController.selectedWallet) return
        walletController.selectedWallet.addressListModel.refresh()
        root.showSectionPage("wallet", true)
        root.pageContainer.push(addressListPage)
    }

    function openWalletPassword(updating) {
        if (!walletController.isWalletLoaded || !walletController.selectedWallet) return
        root.showSectionPage("wallet", true)
        root.pageContainer.push(walletPasswordPage, { "updating": updating })
    }

    function openSignVerifyMessage(initialTab) {
        if (!walletController.isWalletLoaded || !walletController.selectedWallet) return
        root.showSectionPage("wallet", true)
        root.pageContainer.push(signVerifyPage, { "initialTab": initialTab })
    }

    function startWalletBackup() {
        if (!walletController.isWalletLoaded || !walletController.selectedWallet) return
        root.showSectionPage("wallet", true)
        const walletSettingsPage = root.pageContainer.currentItem
        if (walletSettingsPage && typeof walletSettingsPage.startBackup === "function") {
            walletSettingsPage.startBackup()
        }
    }

    background: null
    padding: 0

    onSectionsChanged: ensureVisibleSelection()
    onVisibleChanged: {
        if (visible) root.selectSection(root.selectedSectionId)
    }

    Component.onCompleted: {
        root.selectSection(
            root.sectionIsVisible(root.selectedSectionId) ? root.selectedSectionId : root.firstVisibleSectionId())
        internal.completed = true
    }

    QtObject {
        id: internal
        property bool completed: false
    }

    Connections {
        target: typeof walletController !== "undefined" ? walletController : null

        function onSelectedWalletChanged() {
            root.pageContainer.resetSection("wallet")
        }

        function onIsWalletLoadedChanged() {
            if (!walletController.isWalletLoaded) root.pageContainer.resetSection("wallet")
        }
    }

    contentItem: NavigationSplitView {
        id: splitView
        objectName: "settingsSplitView"
        primaryMinimumWidth: root.sidebarWidth
        primaryPreferredWidth: root.sidebarWidth
        primaryMaximumWidth: root.sidebarWidth
        separatorWidth: 0

        // Narrowing the window keeps the page that was on screen.
        onIsCompactChanged: {
            if (isCompact && internal.completed && root.selectedSectionId.length > 0) showDetail()
        }

        primaryComponent: Rectangle {
            objectName: "settingsSidebarSurface"
            readonly property alias sidebar: sidebar
            color: Theme.color.neutral1

            Behavior on color {
                enabled: Theme.motion.animated
                ColorAnimation { duration: Theme.motion.normal }
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.space.xl
                anchors.rightMargin: Theme.space.xl
                anchors.topMargin: Theme.space.xl
                anchors.bottomMargin: Theme.space.l
                spacing: 0

                CoreText {
                    objectName: "settingsSidebarHeading"
                    Layout.fillWidth: true
                    Layout.leftMargin: 10
                    Layout.rightMargin: 10
                    Layout.bottomMargin: Theme.space.xxl
                    text: qsTr("Settings")
                    color: Theme.color.neutral9
                    font: Theme.text.display.font
                    lineHeight: Theme.text.display.lineHeight
                    lineHeightMode: Text.FixedHeight
                    horizontalAlignment: Text.AlignLeft
                }

                SettingsSidebar {
                    id: sidebar
                    objectName: "settingsSidebar"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: root.sections
                    groupTitles: root.groupTitles
                    // The compact list is a menu of destinations, not a selection.
                    currentSectionId: splitView.isCompact ? "" : root.selectedSectionId
                    onSectionActivated: function(sectionId) {
                        // Re-selecting the current section from the sidebar
                        // returns to its root; pushed sub-pages such as the
                        // address list otherwise survive tab round trips.
                        if (sectionId === root.selectedSectionId) {
                            root.pageContainer.resetSection(sectionId)
                        }
                        root.showSectionPage(sectionId)
                    }
                }

                NavButton {
                    objectName: "settingsDoneButton"
                    visible: root.showDoneButton
                    text: qsTr("Done")
                    Layout.alignment: Qt.AlignHCenter
                    Layout.bottomMargin: Theme.space.xl
                    onClicked: root.doneClicked()
                }
            }
        }

        detailComponent: SettingsPageContainer {
            objectName: "settingsPageContainer"
            showRootBackButton: splitView.isCompact
            onRootBackRequested: splitView.showPrimary()
        }
    }

    Component {
        id: walletPage

        SettingsPages.WalletSectionPage {
            onSelectWalletRequested: root.selectWalletRequested()
            onPasswordRequested: root.pageContainer.push(walletPasswordPage, {
                "updating": walletController.selectedWallet.isEncrypted
            })
            onSignVerifyMessageRequested: root.pageContainer.push(signVerifyPage)
            onCoinsRequested: root.pageContainer.push(coinsPage)
            onAddressesRequested: {
                if (!walletController.isWalletLoaded || !walletController.selectedWallet) return
                walletController.selectedWallet.addressListModel.refresh()
                root.pageContainer.push(addressListPage)
            }
        }
    }

    Component {
        id: coinsPage
        WalletPages.WalletCoins { onBack: root.pageContainer.pop() }
    }

    Component {
        id: walletPasswordPage

        WalletPages.WalletPasswordSettings {
            onBack: root.pageContainer.pop()
            onSaved: root.pageContainer.pop()
        }
    }

    Component {
        id: signVerifyPage

        WalletPages.SignVerifyMessage {
            onBack: root.pageContainer.pop()
        }
    }

    Component {
        id: addressListPage

        WalletPages.AddressList {
            onBack: root.pageContainer.pop()
            // Keep the Addresses page on the stack: leaving for the request
            // view and returning to settings should land back here, not on
            // the settings root.
            onReceiveRequested: root.receiveRequested()
            onPaymentRequestRequested: function(requestId) { root.paymentRequestRequested(requestId) }
        }
    }

    Component {
        id: externalSignerPage
        SettingsPages.ExternalSignerSettingsPage {}
    }

    Component {
        id: displayPage
        SettingsPages.DisplaySettingsPage {}
    }

    Component {
        id: windowBehaviorPage
        SettingsPages.WindowBehaviorSettingsPage {}
    }

    Component {
        id: storagePage
        SettingsPages.StorageSettingsPage {}
    }

    Component {
        id: connectionPage
        SettingsPages.ConnectionSettingsPage {}
    }

    Component {
        id: networkTrafficPage

        SettingsPages.NetworkTrafficSettingsPage {}
    }

    Component {
        id: mempoolPage
        SettingsPages.MempoolSettingsPage {}
    }

    Component {
        id: rpcConsolePage

        SettingsPages.SettingsRpcConsoleView {
            walletName: typeof walletController !== "undefined"
                && walletController.isWalletLoaded && walletController.selectedWallet
                ? walletController.selectedWallet.name
                : ""
        }
    }

    Component {
        id: debugLogPage

        SettingsPages.SettingsDebugLogView {}
    }

    Component {
        id: aboutPage
        SettingsPages.AboutSettingsPage {}
    }
}

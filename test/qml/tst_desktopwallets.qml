// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Window 2.15
import QtTest 1.2
import org.bitcoincore.qt 1.0
import "../../qml/controls"
import "../../qml/pages/wallet"

TestCase {
    name: "DesktopWallets"
    when: windowShown
    visible: true
    width: 900
    height: 600

    Window {
        id: testWindow
        width: 900
        height: 600
        visible: true
    }

    Component {
        id: desktopWalletsComponent

        DesktopWallets {
            width: 900
            height: 600
        }
    }

    function init() {
        walletController.reset()
        walletController.initialized = true
        walletController.isWalletLoaded = true
        walletController.noWalletsFound = false
        walletController.setSelectedWalletObject(testWalletModel)
        walletListModel.reset()
        nodeModel.numPeers = 0
        nodeModel.initialSyncComplete = false
        nodeModel.verificationProgress = 0
        nodeModel.remainingSyncTime = 0
        nodeModel.headerSyncActive = false
        nodeModel.headerPresync = false
        nodeModel.pause = false
        nodeModel.faulted = false
        nodeModel.blockTipHeight = 0
    }

    function test_widgets_and_node_routes_and_peers_navigation() {
        const page = createDesktopWallets()
        const widgets = findChild(page, "widgetsTabButton")
        const node = findChild(page, "blockClockTabButton")
        const overview = findChild(page, "nodeOverview")
        const dashboard = findChild(page, "widgetDashboard")
        verify(widgets !== null)
        compare(findChild(page, "peersTabButton"), null)
        compare(overview.visible, true)
        compare(dashboard.visible, false)
        widgets.checked = true
        tryCompare(dashboard, "visible", true)
        compare(overview.visible, false)
        page.openPeers()
        compare(node.checked, true)
        const stack = findChild(page, "nodeNavigationStack")
        tryCompare(stack, "depth", 2)
        tryCompare(stack, "busy", false)
        compare(stack.currentItem.objectName, "peers")
        compare(overview.visible, false)
        compare(peerTableModel.autoRefreshActive, true)
        const back = findChild(stack.currentItem, "peersNodeBackButton")
        compare(back.text, "Node")
        mouseClick(back)
        tryCompare(stack, "depth", 1)
        tryCompare(stack, "busy", false)
        compare(peerTableModel.autoRefreshActive, false)
        compare(node.checked, true)
        compare(overview.visible, true)
    }

    function createDesktopWallets() {
        const page = createTemporaryObject(desktopWalletsComponent, this)
        verify(page !== null)
        const popup = findChild(page, "walletSelectPopup")
        verify(popup !== null)
        const badge = findChild(page, "walletBadge")
        verify(badge !== null)
        return page
    }

    function test_wallet_badge_refreshes_wallet_list_once_before_opening() {
        const page = createDesktopWallets()
        const popup = findChild(page, "walletSelectPopup")
        const badge = findChild(page, "walletBadge")

        verify(waitForItemPolished(page))
        compare(walletListModel.listWalletDirCalls, 0)

        mouseClick(badge, badge.width / 2, badge.height / 2)
        compare(walletListModel.listWalletDirCalls, 1)
        tryCompare(popup, "opened", true)

        mouseClick(badge, badge.width / 2, badge.height / 2)
        compare(walletListModel.listWalletDirCalls, 1)
        tryCompare(popup, "opened", false)
        tryCompare(popup, "visible", false)

        mouseClick(badge, badge.width / 2, badge.height / 2)
        compare(walletListModel.listWalletDirCalls, 2)
        tryCompare(popup, "opened", true)

        mouseClick(page, page.width - 10, page.height - 10)
        tryCompare(popup, "visible", false)
        compare(walletListModel.listWalletDirCalls, 2)
    }

    function test_explicit_open_wallet_selection_refreshes_wallet_list() {
        const page = createDesktopWallets()
        const popup = findChild(page, "walletSelectPopup")

        page.openWalletSelection()
        compare(walletListModel.listWalletDirCalls, 1)
        tryCompare(popup, "opened", true)
    }

    function test_wallet_badge_balance_uses_money_font() {
        const page = createDesktopWallets()
        const balanceText = findChild(page, "walletBadgeBalanceText")
        verify(balanceText !== null)

        compare(balanceText.font.family, optionsModel.moneyFont.family)
        compare(balanceText.font.weight, optionsModel.moneyFont.weight)
    }

    function test_wallet_badge_balance_updates_unit_suffix() {
        const originalUnit = optionsModel.displayUnit
        const page = createDesktopWallets()
        const balanceText = findChild(page, "walletBadgeBalanceText")
        verify(balanceText !== null)
        try {
            for (const unit of [0, 3, 0]) {
                optionsModel.displayUnit = unit
                for (const satoshi of [0, 1, 2, 1000]) {
                    findChild(page, "walletBadge").balanceSatoshi = satoshi
                    compare(balanceText.text, balanceText.amount + (unit === 3
                        ? (satoshi === 1 ? " sat" : " sats") : " BTC"))
                }
            }
        } finally {
            optionsModel.displayUnit = originalUnit
        }
    }

    function test_desktop_top_nav_icon_buttons_match_design_size() {
        const page = createDesktopWallets()
        const tabs = [
            findChild(page, "blockClockTabButton"),
            findChild(page, "widgetsTabButton"),
            findChild(page, "desktopWalletSettingsTabButton")
        ]

        for (let i = 0; i < tabs.length; ++i) {
            verify(tabs[i] !== null)
            tryCompare(tabs[i], "width", 40)
            compare(tabs[i].height, 60)
        }

        compare(tabs[1].iconSize, 18)
        compare(tabs[1].iconSource, "image://images/widgets.svg")
        compare(tabs[2].iconSize, 30)
        compare(tabs[2].iconSource, "image://images/gear-outline")
        compare(findChild(page, "consoleTabButton"), null)
        compare(findChild(page, "desktopWalletSettingsPreviewTabButton"), null)
        compare(findChild(page, "nodeWarningsButton"), null)
        compare(findChild(page, "nodeInformationButton"), null)
    }

    function test_network_chip_is_read_only() {
        const page = createDesktopWallets()
        const networkIndicator = findChild(page, "desktopNetworkIndicator")
        const informationPopup = findChild(page, "nodeOverviewInformationPopup")
        verify(networkIndicator !== null)
        verify(informationPopup !== null)

        compare(networkIndicator.enabled, false)
        compare(networkIndicator.focusPolicy, Qt.NoFocus)
        networkIndicator.clicked()
        compare(informationPopup.opened, false)
    }

    function test_settings_is_lazilyLoadedAndRetained() {
        const page = createDesktopWallets()
        const settingsTab = findChild(page, "desktopWalletSettingsTabButton")
        const settingsLoader = findChild(page, "settingsLoader")

        verify(settingsTab !== null)
        verify(settingsLoader !== null)
        compare(settingsLoader.active, false)
        compare(settingsLoader.item, null)

        settingsTab.checked = true
        tryCompare(settingsTab, "checked", true)
        tryCompare(settingsLoader, "active", true)
        tryVerify(function() { return settingsLoader.item !== null })
        compare(settingsLoader.item.objectName, "settingsView")
        const settingsView = settingsLoader.item

        settingsTab.checked = false
        compare(settingsLoader.item, settingsView)
        compare(settingsLoader.active, true)
    }

    function test_full_block_clock_is_active_only_on_its_tab() {
        const page = createTemporaryObject(desktopWalletsComponent, testWindow.contentItem)
        verify(page !== null)
        const clock = findChild(page, "blockClock")
        const sendTab = findChild(page, "sendTabButton")
        const blockClockTab = findChild(page, "blockClockTabButton")
        verify(clock !== null)
        verify(sendTab !== null)
        verify(blockClockTab !== null)

        tryCompare(page, "visible", true)
        compare(blockClockTab.checked, true)
        tryCompare(clock, "renderingActive", true)

        sendTab.checked = true
        tryCompare(sendTab, "checked", true)
        tryCompare(clock, "renderingActive", false)

        blockClockTab.checked = true
        tryCompare(blockClockTab, "checked", true)
        tryCompare(clock, "renderingActive", true)
    }

    function test_block_clock_tooltip_uses_latched_sync_completion() {
        nodeModel.numPeers = 1
        nodeModel.verificationProgress = 0.9999
        nodeModel.blockTipHeight = 313899

        const page = createDesktopWallets()
        const tooltip = findChild(page, "blockClockTooltip")
        verify(tooltip !== null)

        compare(tooltip.text, "Downloading blocks\nEstimating")

        nodeModel.initialSyncComplete = true
        wait(0)

        compare(tooltip.text, "Blocktime\n" + Number(nodeModel.blockTipHeight).toLocaleString(Qt.locale(), "f", 0))
    }

    function test_receive_view_addresses_opens_address_settings() {
        const page = createDesktopWallets()
        findChild(page, "receiveTabButton").clicked()
        findChild(page, "receiveMoreButton").clicked()
        tryCompare(findChild(page, "receiveMoreMenu"), "opened", true)
        const historyButton = findChild(page, "requestPaymentHistoryButton")
        verify(historyButton !== null)
        historyButton.clicked()
        compare(findChild(page, "desktopWalletSettingsTabButton").checked, true)
        tryCompare(findChild(page, "settingsPageContainer"), "depth", 2)
        compare(findChild(page, "settingsPageContainer").currentItem.objectName, "addressListPage")
    }
    function test_addresses_settings_preserves_details_popup() {
        const page = createDesktopWallets()
        page.openSettingsRoute("addresses")
        wait(0)
        const settingsTab = findChild(page, "desktopWalletSettingsTabButton")
        verify(settingsTab !== null)
        compare(settingsTab.checked, true)

        const settingsPage = findChild(page, "settingsView")
        verify(settingsPage !== null)

        const settingsContainer = findChild(page, "settingsPageContainer")
        verify(settingsContainer !== null)
        tryCompare(settingsPage, "selectedSectionId", "wallet")
        tryCompare(settingsContainer, "currentSectionId", "wallet")
        tryCompare(settingsContainer, "depth", 2)
        compare(settingsContainer.currentItem.objectName, "addressListPage")
        verify(findChild(page, "walletSettingsPage") !== null)

        const introduction = findChild(page, "addressListIntroduction")
        verify(introduction !== null)
        compare(introduction.descriptionTextFormat, Text.StyledText)
        compare(introduction.description,
            "View addresses generated by this wallet.<br><br>"
            + "<b>Single-use</b> addresses are intended for receiving one payment; avoiding address reuse helps protect your privacy.<br>"
            + "<b>Change</b> addresses are created and managed automatically when you send bitcoin, receiving any remaining amount back into your wallet.")

        const detailsPopup = findChild(page, "addressDetailsPopup")
        verify(detailsPopup !== null)
        compare(detailsPopup.background.color, Theme.color.neutral1)
        compare(detailsPopup.background.border.color, Theme.color.neutral3)
        compare(detailsPopup.modalOverlayColor, Qt.rgba(0, 0, 0, 0.4))
        verify(detailsPopup.enter !== null)
        verify(detailsPopup.exit !== null)

        detailsPopup.open()
        tryCompare(detailsPopup, "opened", true)
        tryCompare(detailsPopup, "opacity", 1)
        compare(detailsPopup.verticalOffset, 0)
        compare(detailsPopup.parent, Overlay.overlay)
        compare(detailsPopup.x, Math.round((Overlay.overlay.width - detailsPopup.width) / 2))
        compare(detailsPopup.y, Math.round((Overlay.overlay.height - detailsPopup.height) / 2))

        detailsPopup.close()
        tryCompare(detailsPopup, "visible", false)
    }
}

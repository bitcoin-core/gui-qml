// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.2
import "../../qml/pages/wallet"

TestCase {
    name: "DesktopWallets"
    when: windowShown
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
        walletListModel.reset()
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

        compare(walletListModel.listWalletDirCalls, 0)

        badge.clicked()
        compare(walletListModel.listWalletDirCalls, 1)
        tryCompare(popup, "opened", true)

        badge.clicked()
        compare(walletListModel.listWalletDirCalls, 1)
        tryCompare(popup, "opened", false)

        badge.clicked()
        compare(walletListModel.listWalletDirCalls, 2)
        tryCompare(popup, "opened", true)
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

    function test_desktop_top_nav_icon_buttons_match_design_size() {
        const page = createDesktopWallets()
        const tabs = [
            findChild(page, "blockClockTabButton"),
            findChild(page, "peersTabButton"),
            findChild(page, "consoleTabButton"),
            findChild(page, "desktopWalletSettingsTabButton")
        ]

        for (let i = 0; i < tabs.length; ++i) {
            verify(tabs[i] !== null)
            tryCompare(tabs[i], "width", 30)
            compare(tabs[i].height, 60)
        }

        compare(tabs[1].iconSize, 24)
        compare(tabs[2].iconSize, 24)
        compare(tabs[3].iconSize, 30)
    }

    function test_console_autocomplete_closes_when_switching_tabs() {
        const page = createDesktopWallets()
        const consoleTab = findChild(page, "consoleTabButton")
        const activityTab = findChild(page, "activityTabButton")
        const popup = findChild(page, "consoleAutocompletePopup")

        verify(consoleTab !== null)
        verify(activityTab !== null)
        verify(popup !== null)

        consoleTab.checked = true
        tryCompare(consoleTab, "checked", true)
        popup.open()
        tryCompare(popup, "visible", true)

        activityTab.checked = true
        tryCompare(activityTab, "checked", true)
        tryCompare(popup, "visible", false)
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

    function test_receive_options_view_address_history_opens_settings_address_stack() {
        const page = createDesktopWallets()
        const receiveTab = findChild(page, "receiveTabButton")
        verify(receiveTab !== null)
        receiveTab.clicked()

        const optionsButton = findChild(page, "receiveOptionsButton")
        verify(optionsButton !== null)
        optionsButton.clicked()

        const popup = findChild(page, "receiveOptionsPopup")
        verify(popup !== null)
        tryCompare(popup, "opened", true)

        const viewHistoryButton = findChild(page, "receiveOptionsViewAddressHistoryButton")
        verify(viewHistoryButton !== null)
        viewHistoryButton.clicked()

        const settingsTab = findChild(page, "desktopWalletSettingsTabButton")
        verify(settingsTab !== null)
        compare(settingsTab.checked, true)

        const settingsPage = findChild(page, "nodeSettingsStack")
        verify(settingsPage !== null)

        tryVerify(function() { return findChild(page, "walletSettingsStack") !== null })
        const walletStack = findChild(page, "walletSettingsStack")
        tryCompare(walletStack, "depth", 2)
        compare(walletStack.currentItem.objectName, "addressListPage")
        verify(findChild(page, "walletSettingsPage") !== null)
    }
}

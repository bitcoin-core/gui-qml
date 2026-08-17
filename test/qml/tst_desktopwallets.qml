// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtTest 1.2
import "../../qml/controls"
import "../../qml/pages/wallet"

TestCase {
    name: "DesktopWallets"
    when: windowShown
    width: 900
    height: 600

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
            findChild(page, "desktopWalletSettingsTabButton")
        ]

        for (let i = 0; i < tabs.length; ++i) {
            verify(tabs[i] !== null)
            tryCompare(tabs[i], "width", 30)
            compare(tabs[i].height, 60)
        }

        compare(tabs[1].iconSize, 24)
        compare(tabs[2].iconSize, 30)
        compare(tabs[2].iconSource, "image://images/gear-outline")
        compare(findChild(page, "consoleTabButton"), null)
        compare(findChild(page, "desktopWalletSettingsPreviewTabButton"), null)
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

        const settingsPage = findChild(page, "settingsView")
        verify(settingsPage !== null)

        const settingsContainer = findChild(page, "settingsPageContainer")
        verify(settingsContainer !== null)
        tryCompare(settingsPage, "selectedSectionId", "wallet")
        tryCompare(settingsContainer, "currentSectionId", "wallet")
        tryCompare(settingsContainer, "depth", 2)
        compare(settingsContainer.currentItem.objectName, "addressListPage")
        verify(findChild(page, "walletSettingsPage") !== null)

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

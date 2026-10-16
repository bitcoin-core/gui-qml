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

    Component {
        id: otherWalletComponent
        QtObject {
            property string name: "other-wallet"
            property bool available: true
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

    function test_wallet_opened_switches_to_activity_data() {
        return [
            { tag: "activity", tab: "activityTabButton" },
            { tag: "send", tab: "sendTabButton" },
            { tag: "receive", tab: "receiveTabButton" },
            { tag: "node", tab: "blockClockTabButton" },
            { tag: "peers", tab: "peersTabButton" },
            { tag: "settings", tab: "desktopWalletSettingsTabButton" }
        ]
    }

    function test_wallet_opened_switches_to_activity(data) {
        const page = createDesktopWallets()
        const tab = findChild(page, data.tab)
        verify(tab !== null)
        tab.checked = true
        compare(tab.checked, true)

        walletController.walletOpened()

        compare(findChild(page, "activityTabButton").checked, true)
    }

    function test_leaving_peers_tab_stops_refresh() {
        const page = createDesktopWallets()
        const peers = findChild(page, "peersTabButton")
        const activity = findChild(page, "activityTabButton")
        verify(peers !== null)
        verify(activity !== null)
        peers.checked = true
        tryCompare(peerTableModel, "autoRefreshActive", true)
        activity.checked = true
        tryCompare(peers, "checked", false)
        tryCompare(peerTableModel, "autoRefreshActive", false)
    }

    function test_close_wallet_confirmation_uses_popup_context() {
        const page = createDesktopWallets()
        const popup = findChild(page, "walletCloseConfirmationPopup")
        verify(popup !== null)
        compare(popup.interactionContext.wallet, popup.targetWallet)
        compare(popup.interactionContext.target, popup.walletName)

        page.requestCloseWallet()
        tryCompare(popup, "opened", true)
        compare(popup.interactionContext.wallet, testWalletModel)
        compare(popup.interactionContext.target, testWalletModel.name)
        verify(popup.contextIsValid())

        const confirm = findChild(popup.contentItem, "walletCloseConfirmationConfirmButton")
        verify(confirm !== null)
        popup.targetWallet = null
        tryCompare(popup, "visible", false)
        confirm.clicked()
        compare(walletController.closeWalletCalls, 0)

        page.requestCloseWallet()
        tryCompare(popup, "opened", true)
        confirm.clicked()
        compare(walletController.closeWalletCalls, 1)
        compare(walletController.lastClosedWalletName, testWalletModel.name)
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

    function test_close_confirmation_tracks_nonselected_wallet_unload() {
        const page = createDesktopWallets()
        const otherWallet = createTemporaryObject(otherWalletComponent, this)
        const popup = findChild(page, "walletCloseConfirmationPopup")
        popup.walletName = otherWallet.name
        popup.targetWallet = otherWallet
        popup.open()
        tryCompare(popup, "opened", true)
        compare(popup.interactionContext.wallet, otherWallet)
        otherWallet.available = false
        compare(walletController.selectedWallet, testWalletModel)
        findChild(popup.contentItem, "walletCloseConfirmationConfirmButton").clicked()
        tryCompare(popup, "visible", false)
        compare(walletController.closeWalletCalls, 0)
    }

    function startDeferredMigration(page, passphrase) {
        walletController.deferWalletMigration = true
        walletController.walletMigrationRequired("legacy-wallet")
        const required = findChild(page, "walletMigrationPopup")
        tryCompare(required, "opened", true)
        findChild(required.contentItem, "walletMigrationConfirmButton").clicked()
        verify(page.migrationAttempt !== null)
        if (!passphrase) return required
        walletController.walletMigrationInProgress = false
        walletController.walletMigrationPassphraseRequired("legacy-wallet")
        const popup = findChild(page, "walletMigrationPassphrasePopup")
        tryCompare(popup, "opened", true)
        popup.submitPassphrase()
        verify(popup.busy)
        verify(page.migrationAttempt !== null)
        return popup
    }

    function test_migration_rejects_stale_results_data() {
        return [
            {tag: "switch-passphrase", ending: "switch", error: "Incorrect passphrase"},
            {tag: "switch-error", ending: "switch", error: "Update failed"},
            {tag: "hidden", ending: "hidden", error: "Incorrect passphrase"},
            {tag: "canceled", ending: "canceled", error: "Update failed"},
            {tag: "replacement-failure", ending: "replacement", error: "Incorrect passphrase"},
            {tag: "replacement-same-path", ending: "replacement", path: "legacy-wallet", error: "Incorrect passphrase"},
            {tag: "replacement-success", ending: "replacement", error: ""}
        ]
    }
    function test_migration_rejects_stale_results(data) {
        const page = createDesktopWallets()
        const popup = startDeferredMigration(page, true)
        const attempt = page.migrationAttempt
        const required = findChild(page, "walletMigrationPopup")
        if (data.ending === "switch") {
            walletController.setSelectedWallet("temporary-wallet")
            walletController.setSelectedWallet(testWalletModel.name)
        } else if (data.ending === "hidden") {
            page.visible = false
            page.visible = true
        } else if (data.ending === "canceled") popup.close()
        else {
            walletController.walletMigrationRequired(data.path || "another-legacy-wallet")
            page.migratePendingWallet("")
        }
        // An invalidated operation still owns its completion slot.
        compare(page.migrationAttempt, attempt)
        walletController.walletMigrationInProgress = false
        walletController.walletMigrationError = data.error
        if (data.error) walletController.walletMigrationFailed()
        else walletController.walletMigrationSucceeded()
        compare(page.migrationAttempt, null)
        tryCompare(popup, "visible", false)
        if (data.ending === "replacement") {
            tryCompare(required, "opened", true)
            compare(required.errorText, "")
            findChild(required.contentItem, "walletMigrationConfirmButton").clicked()
            verify(page.migrationAttempt !== null)
            verify(page.migrationAttempt !== attempt)
        } else tryCompare(required, "visible", false)
    }

    function test_migration_errors_allow_retry_and_completion() {
        const page = createDesktopWallets()
        const popup = startDeferredMigration(page, true)
        walletController.walletMigrationInProgress = false
        walletController.walletMigrationError = "Incorrect passphrase"
        walletController.walletMigrationFailed()
        verify(popup.opened)
        verify(!popup.busy)
        compare(popup.errorText, "Incorrect passphrase")
        popup.submitPassphrase()
        verify(page.migrationAttempt !== null)
        walletController.walletMigrationInProgress = false
        walletController.walletMigrationError = "Update failed"
        walletController.walletMigrationFailed()
        const required = findChild(page, "walletMigrationPopup")
        tryCompare(required, "opened", true)
        compare(required.errorText, "Update failed")
        findChild(required.contentItem, "walletMigrationConfirmButton").clicked()
        verify(page.migrationAttempt !== null)
        walletController.walletMigrationInProgress = false
        walletController.walletMigrationSucceeded()
        tryCompare(popup, "visible", false)
        tryCompare(required, "visible", false)
        compare(page.pendingMigrationPath, "")
        compare(page.migrationAttempt, null)
    }

    function test_leaving_send_cancels_coin_selection() {
        testCoinsListModel.reset()
        const page = createDesktopWallets()
        page.openSend()
        const popup = findChild(page, "coinSelectionPopup")
        popup.open()
        tryCompare(popup, "opened", true)
        testCoinsListModel.toggleCoinSelection(0)
        const done = findChild(popup.contentItem, "coinSelectionDoneButton")
        page.openActivity()
        verify(!popup.contextIsValid())
        done.clicked()
        tryCompare(popup, "visible", false)
        compare(testCoinsListModel.selectedCoinsCount, 0)

        page.openSend()
        popup.open()
        tryCompare(popup, "opened", true)
        testCoinsListModel.toggleCoinSelection(0)
        done.clicked()
        tryCompare(popup, "visible", false)
        compare(testCoinsListModel.selectedCoinsCount, 1)
        testCoinsListModel.reset()
    }

    function test_request_modal_follows_its_presentation_data() {
        return [{tag: "created"}, {tag: "activity"}, {tag: "receive"}]
    }
    function test_request_modal_follows_its_presentation(data) {
        testPaymentRequest.clear()
        testWalletModel.lastRemovedRequestId = ""
        testWalletModel.lastTemplateRequestId = ""
        const page = createDesktopWallets()
        page.openReceive()
        const receive = findChild(page, "requestPaymentPage")
        wait(0)
        receive.draftCard.showRecipientName = true
        const name = findChild(receive.draftCard, "requestPaymentYourNameInput")
        name.text = "Presentation"
        name.textEdited()
        receive.draftCard.createRequest()
        const created = receive.requestModal
        tryCompare(created, "opened", true)
        compare(findChild(page, "activityTabButton").checked, true)
        verify(created.contextIsValid())
        const requestId = created.card.request.id
        let popup = created
        if (data.tag !== "created") {
            created.close()
            tryCompare(created, "visible", false)
            if (data.tag === "activity") {
                const activity = findChild(page, "activityStack")
                activity.openPaymentRequest(requestId)
                popup = findChild(activity, "paymentRequestModal")
            } else {
                page.openReceive()
                verify(created.openRequest(requestId))
            }
            tryCompare(popup, "opened", true)
        }
        findChild(popup.card, "paymentRequestMoreButton").clicked()
        const menu = findChild(popup.card, "paymentRequestMoreMenu")
        tryCompare(menu, "opened", true)
        page.openSend()
        verify(!popup.contextIsValid())
        findChild(popup.card, "requestPaymentDeleteMenuButton").clicked()
        findChild(popup.card, "requestPaymentAgainMenuButton").clicked()
        tryCompare(popup, "visible", false)
        tryCompare(menu, "visible", false)
        compare(testWalletModel.lastRemovedRequestId, "")
        compare(testWalletModel.lastTemplateRequestId, "")
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

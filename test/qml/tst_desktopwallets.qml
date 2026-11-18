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
    id: testCase
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
        testSetup.failOnWarnings()
        walletController.reset()
        walletController.initialized = true
        walletController.isWalletLoaded = true
        walletController.noWalletsFound = false
        walletController.setOpenWallets([testWalletModel])
        walletController.setSelectedWalletObject(testWalletModel)
        testWalletModel.balanceKnown = true
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

    function test_load_error_popup_keeps_original_wallet_and_prepared_message() {
        const page = createDesktopWallets()
        const popup = findChild(page, "walletLoadErrorPopup")
        verify(popup !== null)
        walletController.failLoadForTest("original-wallet", "Prepared original failure")
        tryCompare(popup, "opened", true)
        compare(popup.walletName, "original-wallet")
        compare(popup.failure, walletController.loadFailure)
        compare(popup.message, "Prepared original failure")
        findChild(popup.contentItem, "walletLoadErrorPopupDismissButton").clicked()
        tryCompare(popup, "visible", false)
        compare(walletController.loadFailure, null)
    }

    function test_old_close_prompt_keeps_original_pair_after_same_name_reopen() {
        const page = createDesktopWallets()
        const oldName = testWalletModel.name
        const replacementName = testSecondWalletModel.name
        try {
            testWalletModel.available = true
            testSecondWalletModel.available = true
            testSecondWalletModel.name = oldName
            walletController.requestCloseWallet(testWalletModel)
            const questions = findChild(page, "walletCloseQuestions")
            verify(questions !== null)
            tryCompare(questions, "count", 1)
            // The popup belongs to this immutable question delegate, while its
            // visual parent is the window Overlay rather than the page tree.
            const originalQuestionView = questions.itemAt(0)
            verify(originalQuestionView !== null)
            const popup = originalQuestionView.confirmation
            verify(popup !== null)
            compare(popup.objectName, "walletCloseConfirmationPopup")
            tryCompare(popup, "opened", true)
            const originalQuestion = popup.question
            verify(originalQuestion !== null)
            compare(popup.targetWallet, testWalletModel)

            // Hold the old presentation while its original facade is already
            // fenced and a distinct facade with the same name becomes selected.
            testWalletModel.available = false
            walletController.setOpenWallets([testSecondWalletModel])
            walletController.setSelectedWalletObject(testSecondWalletModel)
            testSecondWalletModel.requestClose()
            const replacementQuestion = testSecondWalletModel.closeQuestion
            verify(replacementQuestion !== null)
            compare(popup.targetWallet, testWalletModel)
            compare(popup.question, originalQuestion)
            verify(popup.contentItem !== null)
            // Popup's visual contents are hosted by its popup item. Resolve the
            // actual button in this original popup's content, not its QObject tree.
            const confirm = findChild(popup.contentItem, "walletCloseConfirmationConfirmButton")
            verify(confirm !== null)
            mouseClick(confirm)
            tryCompare(popup, "visible", false)
            compare(walletController.closeWalletCalls, 0)
            compare(testSecondWalletModel.closeQuestion, replacementQuestion)
            compare(testSecondWalletModel.available, true)
            compare(walletController.selectedWallet, testSecondWalletModel)
        } finally {
            walletController.reset()
            testWalletModel.available = true
            testWalletModel.clearCloseQuestionForTest()
            testSecondWalletModel.available = true
            testSecondWalletModel.clearCloseQuestionForTest()
            testSecondWalletModel.name = replacementName
        }
    }

    function test_migration_error_uses_typed_original_wallet_notice() {
        walletController.holdMigration = true
        const page = createDesktopWallets()
        walletController.migrate("legacy-original")
        const original = walletController.lastMigration
        verify(original !== null)
        walletController.requestMigrationPassphrase("Prepared short message")
        tryVerify(function() { return testSetup.findVisualDescendant(testCase.Window.window, "walletMigrationPassphrasePopup") !== null })
        const popup = testSetup.findVisualDescendant(testCase.Window.window, "walletMigrationPassphrasePopup")
        verify(popup !== null)
        tryCompare(popup, "opened", true)
        const text = findChild(popup.contentItem, "walletMigrationPassphraseErrorText")
        verify(text !== null)
        compare(text.text, "Prepared short message")
        const details = popup.errorDetailsModel
        verify(details !== null)
        compare(text.text, "Prepared short message")
        compare(popup.errorDetailsModel, details)
        compare(original.error.message, "Prepared short message")
    }

    function test_stale_prompt_cannot_authorize_a_successor_data() {
        return [{tag: "confirm", passphrase: false}, {tag: "passphrase", passphrase: true}]
    }
    function test_stale_prompt_cannot_authorize_a_successor(data) {
        walletController.holdMigration = true
        const page = createDesktopWallets()
        walletController.migrate("original-wallet")
        const original = walletController.lastMigration
        const confirmation = original.prompt
        if (data.passphrase) {
            verify(confirmation.confirm().acceptedForDelivery)
            walletController.requestMigrationPassphrase()
        }
        const prompt = original.prompt
        walletController.migrate("successor-wallet")
        const successor = walletController.lastMigration
        verify(successor !== original)
        const receipt = prompt.decline()
        verify(receipt.acceptedForDelivery)
        tryCompare(original, "phase", WalletOperationModel.Cancelled)
        const stale = prompt.submit("old original password")
        verify(!stale.acceptedForDelivery)
        compare(successor.phase, WalletOperationModel.AwaitingConfirmation)
        compare(successor.targetWalletId, "successor-wallet")
        compare(walletController.selectedWallet, testWalletModel)
    }
    function test_migration_result_does_not_select_before_explicit_open() {
        walletController.holdMigration = true
        const page = createDesktopWallets()
        walletController.migrate("legacy-original")
        const operation = walletController.lastMigration
        const prompt = operation.prompt
        verify(prompt.confirm().acceptedForDelivery)
        walletController.setOpenWallets([testWalletModel, testSecondWalletModel])
        walletController.openedWallet = testSecondWalletModel
        walletController.finishMigration()
        tryVerify(function() { return testSetup.findVisualDescendant(testCase.Window.window, "walletMigrationResultPopup") !== null })
        const popup = testSetup.findVisualDescendant(testCase.Window.window, "walletMigrationResultPopup")
        verify(popup !== null)
        tryCompare(popup, "opened", true)
        compare(operation.resultWallet, testSecondWalletModel)
        compare(walletController.selectedWallet, testWalletModel)
        const button = findChild(popup.contentItem, "walletMigrationOpenResultButton")
        verify(button !== null)
        // The 600 px test window is shorter than the dialog's preferred height;
        // its actions must still be inside it, or a click lands outside and
        // dismisses it.
        const buttonBottom = button.mapToItem(null, 0, button.height).y
        const popupBottom = popup.contentItem.mapToItem(null, 0, popup.contentItem.height).y
        verify(buttonBottom <= popupBottom, "Open wallet ends at " + buttonBottom + ", below the dialog at " + popupBottom)
        const retired = testSetup.watchDestruction(popup)
        try {
            verify(retired.count > 0)
            mouseClick(button)
            tryCompare(walletController, "selectedWallet", testSecondWalletModel)
            // Dismissing the owned result removes its immutable delegate.
            // Observe actual teardown rather than querying a deleted wrapper.
            tryCompare(retired, "pending", 0)
        } finally {
            testSetup.releaseDestructionWatch(retired)
        }
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

    function test_unknown_balance_is_not_presented_as_zero() {
        const page = createDesktopWallets()
        const balance = findChild(page, "walletBadgeBalanceText")
        const unavailable = findChild(page, "walletBadgeBalanceUnavailable")
        verify(balance !== null)
        verify(unavailable !== null)
        testWalletModel.balanceKnown = false
        tryCompare(balance, "visible", false)
        tryCompare(unavailable, "visible", true)
        testWalletModel.balanceKnown = true
        tryCompare(balance, "visible", true)
        tryCompare(unavailable, "visible", false)
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
        const view = page.activeView
        findChild(view, "receiveMoreButton").clicked()
        tryCompare(findChild(view, "receiveMoreMenu"), "opened", true)
        const historyButton = findChild(view, "requestPaymentHistoryButton")
        verify(historyButton !== null)
        historyButton.clicked()
        compare(findChild(page, "desktopWalletSettingsTabButton").checked, true)
        const walletSettings = page.activeView.settingsStack
        tryCompare(walletSettings, "parent", page.walletSettingsHost)
        tryCompare(walletSettings, "depth", 2)
        compare(walletSettings.currentItem.objectName, "addressListPage")
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
        compare(settingsContainer.currentItem.objectName, "walletSettingsHost")
        const walletSettings = page.activeView.settingsStack
        tryCompare(walletSettings, "parent", page.walletSettingsHost)
        tryCompare(walletSettings, "depth", 2)
        compare(walletSettings.currentItem.objectName, "addressListPage")
        compare(walletSettings.get(0).objectName, "walletSettingsPage")

        const introduction = findChild(walletSettings, "addressListIntroduction")
        verify(introduction !== null)
        compare(introduction.descriptionTextFormat, Text.StyledText)
        compare(introduction.description,
            "View addresses generated by this wallet.<br><br>"
            + "<b>Single-use</b> addresses are intended for receiving one payment; avoiding address reuse helps protect your privacy.<br>"
            + "<b>Change</b> addresses are created and managed automatically when you send bitcoin, receiving any remaining amount back into your wallet.")

        const detailsPopup = findChild(walletSettings, "addressDetailsPopup")
        verify(detailsPopup !== null)
        compare(detailsPopup.background.color, Theme.color.surface1)
        compare(detailsPopup.background.border.color, Theme.color.surface2)
        verify(detailsPopup.enterTransition !== null)
        verify(detailsPopup.exitTransition !== null)

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

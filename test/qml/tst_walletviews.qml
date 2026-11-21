// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtTest 1.2
import org.bitcoincore.qt 1.0
import "../../qml/controls"
import "../../qml/pages/wallet"

TestCase {
    id: testCase
    name: "WalletViews"
    when: windowShown
    visible: true
    width: 1000
    height: 700

    Component {
        id: desktopWalletsComponent

        DesktopWallets {
            width: testCase.width
            height: testCase.height
        }
    }

    function init() {
        testSetup.failOnWarnings()
        walletController.reset()
        walletController.initialized = true
        walletController.isWalletLoaded = true
        walletController.noWalletsFound = false
        walletController.setOpenWallets([testWalletModel, testSecondWalletModel])
        walletController.setSelectedWalletObject(testWalletModel)
        walletListModel.reset()
    }

    function cleanup() {
        walletController.setOpenWallets([testWalletModel])
        walletController.setSelectedWalletObject(testWalletModel)
        testWalletModel.currentPaymentRequest.label = ""
        testSecondWalletModel.currentPaymentRequest.label = ""
    }

    function test_close_question_presentation_uses_original_context_data() {
        return [{tag: "another-opening", sameOpening: false}, {tag: "new-question", sameOpening: true}]
    }

    function test_close_question_presentation_uses_original_context(data) {
        testWalletModel.available = true
        testSecondWalletModel.available = true
        const page = createShell()
        walletController.requestCloseWallet(testWalletModel)
        const questions = findChild(page, "walletCloseQuestions")
        verify(questions !== null)
        let originalPopup = null
        tryVerify(function() {
            originalPopup = questions.count === 1 ? questions.itemAt(0).confirmation : null
            return originalPopup && originalPopup.opened
        })
        const originalToken = originalPopup.questionToken
        const oldAction = findChild(originalPopup.contentItem, "walletCloseConfirmationConfirmButton")
        verify(oldAction !== null)
        const next = data.sameOpening ? testWalletModel : testSecondWalletModel
        const before = walletController.closeWalletCalls
        Qt.callLater(function() {
            if (oldAction && typeof oldAction.clicked === "function") oldAction.clicked()
        })
        walletController.requestCloseWallet(next)
        let currentPopup = null
        tryVerify(function() {
            currentPopup = questions.count === 1 ? questions.itemAt(0).confirmation : null
            return currentPopup && currentPopup !== originalPopup && currentPopup.opened
        })
        compare(currentPopup.targetWallet, next)
        compare(walletController.closeWalletCalls, before)
        testWalletModel.answerClose(originalToken, true)
        compare(walletController.closeWalletCalls, before)
        findChild(currentPopup.contentItem, "walletCloseConfirmationConfirmButton").clicked()
        compare(walletController.closeWalletCalls, before + 1)
        compare(walletController.lastClosedWalletName, next.name)
    }

    function createShell() {
        const page = createTemporaryObject(desktopWalletsComponent, testCase)
        verify(page !== null)
        tryCompare(page.walletViews, "count", 2)
        return page
    }

    function countByObjectName(item, objectName) {
        let count = item.objectName === objectName ? 1 : 0
        for (let index = 0; index < item.children.length; ++index) {
            count += countByObjectName(item.children[index], objectName)
        }
        return count
    }

    function test_each_open_wallet_gets_its_own_view() {
        const page = createShell()
        const first = page.viewForWallet(testWalletModel)
        const second = page.viewForWallet(testSecondWalletModel)
        verify(first !== null)
        verify(second !== null)
        verify(first !== second)
        compare(first.sendPage.wallet, testWalletModel)
        compare(second.sendPage.wallet, testSecondWalletModel)
        compare(first.receivePage.wallet, testWalletModel)
        compare(second.settingsStack.wallet, testSecondWalletModel)
        compare(page.activeView, first)
        compare(first.active, true)
        compare(second.active, false)
        findChild(page, "activityTabButton").checked = true
        compare(first.visible, true)
        compare(second.visible, false)
    }

    function test_node_pages_exist_once() {
        const page = createShell()
        findChild(page, "desktopWalletSettingsTabButton").checked = true
        tryVerify(function() { return findChild(page, "settingsView") !== null })
        compare(countByObjectName(page, "blockClock"), 1)
        compare(countByObjectName(page, "settingsView"), 1)
        compare(countByObjectName(page, "peers"), 1)
        compare(countByObjectName(page, "walletView"), 2)
    }

    // Popups, menus and native file dialogs of the first wallet, opened
    // while it is shown. Each must close when another wallet is shown.
    function openFirstWalletPopups(page, first, dialogName) {
        findChild(page, "sendTabButton").checked = true
        const popups = {
            review: findChild(first.sendPage, "transactionReviewPopup"),
            sendPassphrase: findChild(first.sendPage, "sendTransactionPassphrasePopup"),
            receiveMenu: findChild(first.receivePage, "receiveMoreMenu"),
        }
        const dialogs = {
            exportCsv: findChild(first.activityPage, "activityExportDialog"),
            psbtOpen: findChild(first.sendPage, "sendPsbtOpenDialog"),
            psbtSave: findChild(first.sendPage, "sendTransactionReviewSaveDialog"),
            requestQrSave: findChild(first.receivePage, "requestPaymentSaveQRDialog"),
            backup: findChild(first.settingsStack, "walletSettingsBackupDialog"),
        }
        for (const name in popups) {
            verify(popups[name] !== null, name)
            popups[name].open()
            tryCompare(popups[name], "opened", true, 5000, name)
        }
        for (const name in dialogs) {
            verify(dialogs[name] !== null, name)
            if (name === dialogName) {
                if (name === "backup") page.openSettingsRoute("wallet")
                verify(dialogs[name].open(), name)
                compare(dialogs[name].visible, true, name)
            } else compare(dialogs[name].visible, false, name)
        }
        return { popups: popups, dialogs: dialogs }
    }

    function test_switching_wallet_drops_keyboard_focus_from_hidden_view() {
        const page = createShell()
        const first = page.viewForWallet(testWalletModel)
        findChild(page, "sendTabButton").checked = true
        const note = findChild(first.sendPage, "sendNoteInput")
        verify(note !== null)
        note.text = ""
        note.forceActiveFocus()
        verify(note.activeFocus)

        // Selecting another wallet without touching the window, as an RPC
        // loadwallet does, must not leave typing going to the hidden view.
        walletController.setSelectedWalletObject(testSecondWalletModel)
        verify(!note.activeFocus)
        keyClick(Qt.Key_X)
        compare(note.text, "")

        // Leaving the wallet pages for the node page drops it too.
        walletController.setSelectedWalletObject(testWalletModel)
        note.forceActiveFocus()
        verify(note.activeFocus)
        page.openNode()
        verify(!note.activeFocus)
        keyClick(Qt.Key_Y)
        compare(note.text, "")
    }

    function test_closed_popup_does_not_return_focus_to_hidden_view() {
        const page = createShell()
        const first = page.viewForWallet(testWalletModel)
        findChild(page, "sendTabButton").checked = true
        testRecipientsModel.allValid = true
        const reviewButton = findChild(first.sendPage, "sendReviewButton")
        const reviewPopup = findChild(first.sendPage, "transactionReviewPopup")
        reviewButton.forceActiveFocus()
        verify(reviewButton.activeFocus)
        reviewPopup.open()
        tryCompare(reviewPopup, "opened", true)
        verify(!reviewButton.activeFocus)

        // The popup closes with the view and would hand focus back to the
        // button it was opened from, which is now hidden.
        walletController.setSelectedWalletObject(testSecondWalletModel)
        tryCompare(reviewPopup, "visible", false)
        wait(50)
        verify(!reviewButton.activeFocus)
        keyClick(Qt.Key_Space)
        wait(50)
        compare(reviewPopup.visible, false)
    }

    function test_switching_wallet_hides_view_and_closes_its_popups_data() {
        return ["exportCsv", "psbtOpen", "psbtSave", "requestQrSave", "backup"].map(function(name) { return {tag: name, dialogName: name} })
    }
    function test_switching_wallet_hides_view_and_closes_its_popups(data) {
        const page = createShell()
        const first = page.viewForWallet(testWalletModel)
        const second = page.viewForWallet(testSecondWalletModel)
        const opened = openFirstWalletPopups(page, first, data.dialogName)

        walletController.setSelectedWalletObject(testSecondWalletModel)
        compare(page.activeView, second)
        compare(first.active, false)
        compare(first.visible, false)
        compare(second.visible, data.dialogName !== "backup")
        if (data.dialogName === "backup") compare(second.settingsStack.visible, true)
        for (const name in opened.popups) tryCompare(opened.popups[name], "visible", false, 5000, name)
        for (const name in opened.dialogs) compare(opened.dialogs[name].visible, false, name)
    }

    function test_hosted_wallet_settings_dialogs_close_on_switch() {
        const page = createShell()
        const first = page.viewForWallet(testWalletModel)
        page.openSettingsRoute("wallet")
        tryVerify(function() { return page.walletSettingsHost !== null })
        tryCompare(first.settingsStack, "parent", page.walletSettingsHost)
        const backup = findChild(first.settingsStack, "walletSettingsBackupDialog")
        verify(backup !== null)
        backup.open()
        compare(backup.visible, true)

        walletController.setSelectedWalletObject(testSecondWalletModel)
        compare(backup.visible, false)
    }

    function test_settings_stack_follows_active_wallet() {
        const page = createShell()
        const first = page.viewForWallet(testWalletModel)
        const second = page.viewForWallet(testSecondWalletModel)
        page.openSettingsRoute("addresses")
        tryCompare(findChild(page, "desktopWalletSettingsTabButton"), "checked", true)
        tryVerify(function() { return page.walletSettingsHost !== null })
        tryCompare(first.settingsStack, "parent", page.walletSettingsHost)
        compare(first.settingsStack.depth, 2)
        compare(first.settingsStack.currentItem.objectName, "addressListPage")

        walletController.setSelectedWalletObject(testSecondWalletModel)
        tryCompare(second.settingsStack, "parent", page.walletSettingsHost)
        verify(first.settingsStack.parent !== page.walletSettingsHost)
        compare(second.settingsStack.depth, 1)

        walletController.setSelectedWalletObject(testWalletModel)
        tryCompare(first.settingsStack, "parent", page.walletSettingsHost)
        compare(first.settingsStack.depth, 2)
    }

    function test_deferred_opens_are_dropped_once_their_wallet_is_hidden() {
        const page = createShell()
        const first = page.viewForWallet(testWalletModel)
        const psbtOpen = findChild(first.sendPage, "sendPsbtOpenDialog")
        const backup = findChild(first.settingsStack, "walletSettingsBackupDialog")

        first.openPsbtImporter()
        walletController.setSelectedWalletObject(testSecondWalletModel)
        wait(50)
        compare(psbtOpen.visible, false)

        // Showing the first wallet again before the deferred open runs
        // does not revive it.
        walletController.setSelectedWalletObject(testWalletModel)
        first.openPsbtImporter()
        walletController.setSelectedWalletObject(testSecondWalletModel)
        walletController.setSelectedWalletObject(testWalletModel)
        wait(50)
        compare(psbtOpen.visible, false)

        page.openSettingsRoute("backup")
        walletController.setSelectedWalletObject(testSecondWalletModel)
        wait(50)
        compare(backup.visible, false)

        walletController.setSelectedWalletObject(testWalletModel)
        page.openSettingsRoute("backup")
        tryCompare(backup, "visible", true)
    }

    function test_leaving_wallet_pages_closes_their_dialogs() {
        const page = createShell()
        const first = page.viewForWallet(testWalletModel)
        const psbtOpen = findChild(first.sendPage, "sendPsbtOpenDialog")
        first.openPsbtImporter()
        tryCompare(psbtOpen, "visible", true)

        page.openNode()
        compare(first.active, true)
        compare(first.visible, false)
        compare(psbtOpen.visible, false)
    }

    function test_hosted_wallet_settings_keep_dialogs_while_shown() {
        const page = createShell()
        const first = page.viewForWallet(testWalletModel)
        const backup = findChild(first.settingsStack, "walletSettingsBackupDialog")
        page.openSettingsRoute("backup")
        tryCompare(backup, "visible", true)
        // The view's pages are hidden behind the settings page, but its
        // settings stack is what is on screen.
        compare(first.visible, false)
        compare(first.settingsStack.visible, true)
        wait(50)
        compare(backup.visible, true)

        // Leaving the wallet section closes it.
        findChild(page, "settingsSidebar_display").clicked()
        compare(backup.visible, false)
    }

    function test_reselecting_wallet_section_returns_to_wallet_settings_root() {
        const page = createShell()
        const first = page.viewForWallet(testWalletModel)
        page.openSettingsRoute("addresses")
        tryVerify(function() { return page.walletSettingsHost !== null })
        tryCompare(first.settingsStack, "depth", 2)

        const sidebarWallet = findChild(page, "settingsSidebar_wallet")
        verify(sidebarWallet !== null)
        sidebarWallet.clicked()
        compare(first.settingsStack.depth, 1)
        compare(first.settingsStack.currentItem.objectName, "walletSettingsPage")
    }

    function test_drafts_and_tabs_survive_switching_away_and_back() {
        const page = createShell()
        const first = page.viewForWallet(testWalletModel)
        const second = page.viewForWallet(testSecondWalletModel)

        findChild(page, "sendTabButton").checked = true
        compare(first.currentTab, WalletView.Send)
        const note = findChild(first.sendPage, "sendNoteInput")
        verify(note !== null)
        note.text = "rent"
        testWalletModel.coinsListModel.setManualInputSelection(true)

        testWalletModel.currentPaymentRequest.label = "Alice"
        const firstName = findChild(first.receivePage, "requestPaymentYourNameInput")
        verify(firstName !== null)
        compare(firstName.text, "Alice")

        walletController.setSelectedWalletObject(testSecondWalletModel)
        // The second wallet opens on its own last tab.
        compare(second.currentTab, WalletView.Activity)
        compare(findChild(page, "activityTabButton").checked, true)
        findChild(page, "receiveTabButton").checked = true
        compare(second.currentTab, WalletView.Receive)
        compare(first.currentTab, WalletView.Send)

        walletController.setSelectedWalletObject(testWalletModel)
        compare(page.viewForWallet(testWalletModel), first)
        compare(findChild(page, "sendTabButton").checked, true)
        compare(note.text, "rent")
        compare(first.sendPage.manualCoinSelection, true)
        compare(testWalletModel.currentPaymentRequest.label, "Alice")
        compare(firstName.text, "Alice")
        compare(findChild(second.receivePage, "requestPaymentYourNameInput").text, "")

        // Picking the wallet that is already shown changes nothing.
        walletController.setSelectedWallet("reselect-same-wallet")
        compare(page.viewForWallet(testWalletModel), first)
        compare(testWalletModel.currentPaymentRequest.label, "Alice")
        compare(firstName.text, "Alice")
        compare(note.text, "rent")
    }

    function test_request_signals_reach_only_the_targeted_view() {
        const page = createShell()
        const first = page.viewForWallet(testWalletModel)
        const second = page.viewForWallet(testSecondWalletModel)
        findChild(page, "activityTabButton").checked = true

        walletController.requestOpenReceive(testSecondWalletModel)
        compare(second.currentTab, WalletView.Receive)
        compare(first.currentTab, WalletView.Activity)
        // The shown wallet keeps its tab.
        compare(findChild(page, "activityTabButton").checked, true)

        walletController.requestOpenWalletSettings(testSecondWalletModel)
        compare(findChild(page, "desktopWalletSettingsTabButton").checked, false)

        walletController.requestOpenReceive(testWalletModel)
        compare(first.currentTab, WalletView.Receive)
        compare(findChild(page, "receiveTabButton").checked, true)

        const firstModal = findChild(first.activityPage, "paymentRequestModal")
        const secondModal = findChild(second.activityPage, "paymentRequestModal")
        verify(firstModal !== null)
        verify(secondModal !== null)
        firstModal.open()
        tryCompare(firstModal, "opened", true)
        walletController.requestClosePaymentRequestDetail(testSecondWalletModel)
        wait(50)
        compare(firstModal.visible, true)
        walletController.requestClosePaymentRequestDetail(testWalletModel)
        tryCompare(firstModal, "visible", false)
    }

    function test_unloading_a_wallet_destroys_its_view() {
        const page = createShell()
        const second = page.viewForWallet(testSecondWalletModel)
        let destroyed = false
        second.Component.destruction.connect(function() { destroyed = true })
        // Popups and dialogs of the unloaded wallet go with its view.
        const reviewPopup = findChild(second.sendPage, "transactionReviewPopup")
        let reviewDestroyed = false
        reviewPopup.Component.destruction.connect(function() { reviewDestroyed = true })
        walletController.setSelectedWalletObject(testSecondWalletModel)
        findChild(page, "sendTabButton").checked = true
        const psbtOpen = findChild(second.sendPage, "sendPsbtOpenDialog")
        verify(psbtOpen.open())
        compare(psbtOpen.visible, true)

        walletController.setOpenWallets([testWalletModel])
        tryCompare(page.walletViews, "count", 1)
        verify(destroyed)
        verify(reviewDestroyed)
        compare(page.viewForWallet(testSecondWalletModel), null)
        // The controller owns fallback selection; this view fixture supplies
        // the resulting selection after removing the selected opening.
        walletController.setSelectedWalletObject(testWalletModel)
        compare(page.activeView.wallet, testWalletModel)

        walletController.setOpenWallets([testWalletModel, testSecondWalletModel])
        tryCompare(page.walletViews, "count", 2)
        verify(page.viewForWallet(testSecondWalletModel) !== null)
    }

    function test_open_wallet_shows_that_wallet_only_while_it_is_open() {
        const page = createShell()
        findChild(page, "peersTabButton").checked = true

        page.openWallet(testSecondWalletModel)
        compare(walletController.lastSelectedWalletName, "second")

        walletController.setSelectedWalletObject(testSecondWalletModel)
        page.openWallet(testSecondWalletModel)
        compare(findChild(page, "activityTabButton").checked, true)
        compare(page.activeView.wallet, testSecondWalletModel)

        walletController.setSelectedWalletObject(testWalletModel)
        walletController.setOpenWallets([testWalletModel])
        walletController.reset()
        page.openWallet(testSecondWalletModel)
        compare(walletController.lastSelectedWalletName, "")
    }

    function test_unloading_wallet_loses_its_view_before_it_leaves_the_list() {
        const page = createShell()
        try {
            testSecondWalletModel.available = false
            tryCompare(page.walletViews, "count", 1)
            compare(page.viewForWallet(testSecondWalletModel), null)
            compare(walletController.openWallets.length, 2)
        } finally {
            testSecondWalletModel.available = true
        }
    }

    // The real controller order: the unloading wallet turns unavailable,
    // and on a later event loop pass the next wallet is selected and the
    // unloaded one leaves the open wallet list.
    function test_unloading_the_selected_wallet_shows_the_next_wallet() {
        const page = createShell()
        const second = page.viewForWallet(testSecondWalletModel)
        findChild(page, "sendTabButton").checked = true
        try {
            testWalletModel.available = false
            wait(0)
            compare(page.walletTabSelected, true)
            walletController.setSelectedWalletObject(testSecondWalletModel)
            walletController.setOpenWallets([testSecondWalletModel])
            wait(50)
            compare(page.activeView, second)
            compare(page.walletTabSelected, true)
            compare(findChild(page, "blockClockTabButton").checked, false)
            compare(findChild(page, "activityTabButton").checked, true)
            compare(second.visible, true)
        } finally {
            testWalletModel.available = true
        }
    }

    function test_closing_the_last_wallet_leaves_the_wallet_tabs() {
        const page = createShell()
        findChild(page, "sendTabButton").checked = true
        walletController.setWalletLoaded(false)
        walletController.setOpenWallets([])
        tryCompare(page.walletViews, "count", 0)
        tryCompare(findChild(page, "blockClockTabButton"), "checked", true)
        walletController.setWalletLoaded(true)
    }
}

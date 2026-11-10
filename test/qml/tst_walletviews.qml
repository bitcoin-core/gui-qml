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
    function openFirstWalletPopups(page, first) {
        findChild(page, "sendTabButton").checked = true
        const popups = {
            review: findChild(first.sendPage, "transactionReviewPopup"),
            sendPassphrase: findChild(first.sendPage, "sendTransactionPassphrasePopup"),
            receiveMenu: findChild(first.receivePage, "receiveMoreMenu"),
            backupWallet: findChild(first.settingsStack, "backupWalletPopup"),
            backupReplace: findChild(first.settingsStack, "backupReplaceAlert"),
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
            dialogs[name].open()
            compare(dialogs[name].visible, true, name)
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

    function test_switching_wallet_hides_view_and_closes_its_popups() {
        const page = createShell()
        const first = page.viewForWallet(testWalletModel)
        const second = page.viewForWallet(testSecondWalletModel)
        const opened = openFirstWalletPopups(page, first)

        walletController.setSelectedWalletObject(testSecondWalletModel)
        compare(page.activeView, second)
        compare(first.active, false)
        compare(first.visible, false)
        compare(second.visible, true)
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

    function openWalletSettings(page, view) {
        page.openSettingsRoute("wallet")
        tryVerify(function() { return page.walletSettingsHost !== null })
        tryCompare(view.settingsStack, "parent", page.walletSettingsHost)
    }

    function openBackup(page, first) {
        testWalletModel.resetWalletSettingsTestState()
        walletController.clearTestWalletFileDestinations()
        openWalletSettings(page, first)
        const popup = findChild(first.settingsStack, "backupWalletPopup")
        findChild(first.settingsStack, "walletBackupRow").clicked()
        tryCompare(popup, "opened", true)
        return popup
    }

    function closeBackup(popup) {
        popup.close()
        tryCompare(popup, "visible", false)
        walletController.clearTestWalletFileDestinations()
        testWalletModel.resetWalletSettingsTestState()
    }

    function existingDestination(path, status) {
        return {
            status: status,
            path: path,
            folder: "/tmp",
            fileName: path.substring(path.lastIndexOf("/") + 1),
            keepBothPath: "/tmp/wallet-2026-01-02-1432.bak",
            keepBothFileName: "wallet-2026-01-02-1432.bak",
            error: "",
        }
    }

    function test_backup_saves_full_backup_to_dated_default() {
        const page = createShell()
        const first = page.viewForWallet(testWalletModel)
        const popup = openBackup(page, first)
        compare(findChild(first.settingsStack, "walletBackupRow").title, "Back up or export wallet")
        compare(findChild(popup, "backupTypeFull").checked, true)
        compare(findChild(popup, "backupTypeWatchOnly").checked, false)
        compare(findChild(popup, "backupDestinationPath").text, "/tmp/wallet-2026-01-02.bak")
        compare(findChild(popup, "backupSaveButton").text, "Save backup")

        findChild(popup, "backupSaveButton").clicked()
        compare(testWalletModel.backupWalletCalls, 1)
        compare(testWalletModel.lastBackupPath, "/tmp/wallet-2026-01-02.bak")
        compare(testWalletModel.lastBackupReplace, false)
        tryCompare(popup, "visible", false)
        tryCompare(findChild(first.settingsStack, "walletSettingsResultText"), "text",
                   "Backup saved to /tmp/wallet-2026-01-02.bak")
        closeBackup(popup)
    }

    function test_backup_default_name_follows_type_until_location_is_chosen() {
        const page = createShell()
        const first = page.viewForWallet(testWalletModel)
        const popup = openBackup(page, first)
        const path = findChild(popup, "backupDestinationPath")
        const dialog = findChild(first.settingsStack, "walletSettingsBackupDialog")

        findChild(popup, "backupTypeWatchOnly").clicked()
        compare(path.text, "/tmp/wallet-watchonly-2026-01-02.dat")
        compare(findChild(popup, "backupSaveButton").text, "Save watch-only copy")
        findChild(popup, "backupTypeFull").clicked()
        compare(path.text, "/tmp/wallet-2026-01-02.bak")

        findChild(popup, "backupChangeLocation").clicked()
        compare(dialog.visible, true)
        compare(dialog.confirmOverwrite, false)
        compare(dialog.currentFile.toString(), "file:///tmp/wallet-2026-01-02.bak")
        dialog.selectAndAccept("file:///tmp/mine.bak")
        compare(path.text, "/tmp/mine.bak")
        findChild(popup, "backupTypeWatchOnly").clicked()
        compare(path.text, "/tmp/mine.bak")

        findChild(popup, "backupSaveButton").clicked()
        compare(testWalletModel.watchOnlyExportCalls, 1)
        compare(testWalletModel.lastWatchOnlyExportPath, "/tmp/mine.bak")
        compare(findChild(popup, "backupSaveButton").enabled, false)
        compare(findChild(first.settingsStack, "walletBackupRow").enabled, false)
        testWalletModel.finishWatchOnlyWalletExport("")
        tryCompare(popup, "visible", false)
        tryCompare(findChild(first.settingsStack, "walletSettingsResultText"), "text",
                   "Watch-only copy saved to /tmp/mine.bak")
        closeBackup(popup)
    }

    function test_backup_asks_before_replacing_a_wallet_file() {
        const page = createShell()
        const first = page.viewForWallet(testWalletModel)
        const popup = openBackup(page, first)
        const alert = findChild(first.settingsStack, "backupReplaceAlert")
        const path = "/tmp/wallet-2026-01-02.bak"
        walletController.setTestWalletFileDestination(path, existingDestination(path, "walletFile"))

        findChild(popup, "backupSaveButton").clicked()
        tryCompare(alert, "opened", true)
        compare(testWalletModel.backupWalletCalls, 0)
        compare(alert.title, "Replace wallet file?")
        verify(findChild(alert.contentItem, "backupReplaceMessage").text.indexOf("funds they control could be lost") >= 0)
        compare(findChild(alert.contentItem, "backupReplaceFolder").text, "/tmp")
        compare(findChild(alert.contentItem, "backupReplaceExistingName").text, "wallet-2026-01-02.bak")
        compare(findChild(alert.contentItem, "backupReplaceKeepBothName").text, "wallet-2026-01-02-1432.bak")

        findChild(alert.contentItem, "backupReplaceCancel").clicked()
        tryCompare(alert, "visible", false)
        compare(testWalletModel.backupWalletCalls, 0)
        compare(popup.visible, true)

        findChild(popup, "backupSaveButton").clicked()
        tryCompare(alert, "opened", true)
        findChild(alert.contentItem, "backupReplaceKeepBoth").clicked()
        compare(testWalletModel.backupWalletCalls, 1)
        compare(testWalletModel.lastBackupPath, "/tmp/wallet-2026-01-02-1432.bak")
        compare(testWalletModel.lastBackupReplace, false)
        tryCompare(popup, "visible", false)
        closeBackup(popup)
    }

    function test_backup_replace_overwrites_only_when_chosen() {
        const page = createShell()
        const first = page.viewForWallet(testWalletModel)
        const popup = openBackup(page, first)
        const alert = findChild(first.settingsStack, "backupReplaceAlert")
        const path = "/tmp/wallet-watchonly-2026-01-02.dat"
        walletController.setTestWalletFileDestination(path, existingDestination(path, "otherFile"))
        findChild(popup, "backupTypeWatchOnly").clicked()

        findChild(popup, "backupSaveButton").clicked()
        tryCompare(alert, "opened", true)
        compare(alert.title, "Replace file?")
        findChild(alert.contentItem, "backupReplaceConfirm").clicked()
        compare(testWalletModel.watchOnlyExportCalls, 1)
        compare(testWalletModel.lastWatchOnlyExportPath, path)
        compare(testWalletModel.lastWatchOnlyExportReplace, true)

        testWalletModel.finishWatchOnlyWalletExport("Error: Wallet has no descriptors to export")
        compare(popup.visible, true)
        compare(findChild(popup, "backupWalletErrorText").text, "Error: Wallet has no descriptors to export")
        closeBackup(popup)
    }

    function test_backup_blocked_destination_shows_error() {
        const page = createShell()
        const first = page.viewForWallet(testWalletModel)
        const popup = openBackup(page, first)
        walletController.setTestWalletFileDestination("/tmp/wallet-2026-01-02.bak", {
            status: "blocked",
            error: "Wallet files can't be saved in the wallets folder. Choose another location.",
        })

        findChild(popup, "backupSaveButton").clicked()
        compare(testWalletModel.backupWalletCalls, 0)
        compare(findChild(first.settingsStack, "backupReplaceAlert").visible, false)
        compare(findChild(popup, "backupWalletErrorText").text,
                "Wallet files can't be saved in the wallets folder. Choose another location.")
        closeBackup(popup)
    }

    function test_backup_failure_keeps_sheet_open() {
        const page = createShell()
        const first = page.viewForWallet(testWalletModel)
        const popup = openBackup(page, first)
        testWalletModel.backupTestError = "The wallet could not be backed up."

        findChild(popup, "backupSaveButton").clicked()
        compare(popup.visible, true)
        compare(findChild(popup, "backupWalletErrorText").text, "The wallet could not be backed up.")
        compare(findChild(first.settingsStack, "walletSettingsPage").errorText, "")
        closeBackup(popup)
    }

    function rightEdgeIn(item, container) {
        return item.mapToItem(container, item.width, 0).x
    }

    // Text that cannot wrap is painted past its own width.
    function fitsInside(text, container) {
        return text.contentWidth <= text.width + 1 && rightEdgeIn(text, container) <= container.width
    }

    function test_backup_long_path_wraps_instead_of_widening() {
        testWalletModel.resetWalletSettingsTestState()
        walletController.clearTestWalletFileDestinations()
        const page = createShell()
        const first = page.viewForWallet(testWalletModel)
        openWalletSettings(page, first)
        const folder = "/tmp/" + "averylongfoldernamewithoutanybreaks".repeat(6)
        const longPath = folder + "/wallet-2026-01-02.bak"
        findChild(first.settingsStack, "walletSettingsBackupPathField").text = longPath
        const popup = findChild(first.settingsStack, "backupWalletPopup")
        findChild(first.settingsStack, "walletBackupRow").clicked()
        tryCompare(popup, "opened", true)

        const path = findChild(popup, "backupDestinationPath")
        const change = findChild(popup, "backupChangeLocation")
        compare(path.text, longPath)
        verify(path.lineCount > 1)
        verify(fitsInside(path, popup.contentItem))
        verify(rightEdgeIn(path, popup.contentItem) <= change.mapToItem(popup.contentItem, 0, 0).x)
        verify(rightEdgeIn(change, popup.contentItem) <= popup.contentItem.width)

        const existing = existingDestination(longPath, "walletFile")
        existing.folder = folder
        existing.keepBothFileName = "wallet-" + "x".repeat(120) + ".bak"
        walletController.setTestWalletFileDestination(longPath, existing)
        const alert = findChild(first.settingsStack, "backupReplaceAlert")
        findChild(popup, "backupSaveButton").clicked()
        tryCompare(alert, "opened", true)
        const alertFolder = findChild(alert.contentItem, "backupReplaceFolder")
        const keepBothName = findChild(alert.contentItem, "backupReplaceKeepBothName")
        verify(alertFolder.lineCount > 1)
        verify(keepBothName.lineCount > 1)
        verify(fitsInside(alertFolder, alert.contentItem))
        verify(fitsInside(keepBothName, alert.contentItem))
        verify(rightEdgeIn(findChild(alert.contentItem, "backupReplaceConfirm"), alert.contentItem) <= alert.contentItem.width)

        findChild(alert.contentItem, "backupReplaceConfirm").clicked()
        const result = findChild(first.settingsStack, "walletSettingsResultText")
        tryCompare(result, "text", "Backup saved to " + longPath)
        const banner = findChild(first.settingsStack, "walletSettingsResultBanner")
        tryVerify(() => banner.width > 0 && result.lineCount > 1)
        verify(fitsInside(result, banner))
        closeBackup(popup)
    }

    function test_backup_without_private_keys_offers_only_a_backup() {
        testWalletModel.resetWalletSettingsTestState()
        testWalletModel.setExternalSignerWalletSettingsTestState()
        const page = createShell()
        const first = page.viewForWallet(testWalletModel)
        openWalletSettings(page, first)
        const popup = findChild(first.settingsStack, "backupWalletPopup")
        findChild(first.settingsStack, "walletBackupRow").clicked()
        tryCompare(popup, "opened", true)
        compare(findChild(popup, "backupTypeWatchOnly").visible, false)
        compare(findChild(popup, "backupSaveButton").text, "Save backup")
        closeBackup(popup)
    }

    function descriptorExport(id, label) {
        return {
            id: id,
            label: label,
            description: "",
            scriptName: "wpkh",
            xpub: "tpubDD7VgqKs35ZQein21QPXtb5o8tUngVN1FK5L",
            descriptor: "wpkh(tpubDD7VgqKs35ZQein21QPXtb5o8tUngVN1FK5L/<0;1>/*)#hv6zap77",
            descriptorParts: {},
            extendedKey: "",
            fingerprint: "621f5481",
            derivationPath: "m/84h/1h/0h",
            receiveChain: "0",
            changeChain: "1",
        }
    }

    function test_descriptors_row_pushes_list_then_detail() {
        testWalletModel.resetWalletSettingsTestState()
        testWalletModel.setDescriptorExportsForTest([descriptorExport("bech32m", "Taproot"), descriptorExport("bech32", "SegWit")], [])
        const page = createShell()
        const first = page.viewForWallet(testWalletModel)
        openWalletSettings(page, first)
        const stack = first.settingsStack
        const row = findChild(stack, "walletDescriptorsRow")
        tryCompare(row, "visible", true)
        compare(row.description, "Public descriptors and extended keys (xpub) for watch-only wallets.")

        row.clicked()
        tryCompare(stack, "depth", 2)
        compare(stack.currentItem.objectName, "walletDescriptorsPage")
        stack.currentItem.descriptorRequested("bech32")
        tryCompare(stack, "depth", 3)
        compare(stack.currentItem.objectName, "walletDescriptorDetailPage")
        compare(stack.currentItem.title, "SegWit")

        stack.currentItem.back()
        tryCompare(stack, "depth", 2)
        stack.currentItem.back()
        tryCompare(stack, "depth", 1)
        testWalletModel.resetWalletSettingsTestState()
    }

    function test_descriptors_row_skips_list_for_one_descriptor() {
        testWalletModel.resetWalletSettingsTestState()
        testWalletModel.setDescriptorExportsForTest([descriptorExport("bech32", "SegWit")], [])
        const page = createShell()
        const first = page.viewForWallet(testWalletModel)
        openWalletSettings(page, first)
        const stack = first.settingsStack
        findChild(stack, "walletDescriptorsRow").clicked()
        tryCompare(stack, "depth", 2)
        compare(stack.currentItem.objectName, "walletDescriptorDetailPage")
        compare(stack.currentItem.payload, descriptorExport("bech32", "SegWit").descriptor)

        // Previous keys are only shown in the list.
        stack.resetToRoot()
        testWalletModel.setDescriptorExportsForTest([descriptorExport("bech32", "SegWit")], [descriptorExport("bech32", "SegWit")])
        findChild(stack, "walletDescriptorsRow").clicked()
        tryCompare(stack, "depth", 2)
        compare(stack.currentItem.objectName, "walletDescriptorsPage")
        stack.resetToRoot()
        testWalletModel.resetWalletSettingsTestState()
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
        const backup = findChild(first.settingsStack, "backupWalletPopup")

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
        const backup = findChild(first.settingsStack, "backupWalletPopup")
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
        tryCompare(backup, "visible", false)
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
        first.sendPage.manualCoinSelection = true

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
        const psbtOpen = findChild(second.sendPage, "sendPsbtOpenDialog")
        psbtOpen.open()
        compare(psbtOpen.visible, true)

        walletController.setOpenWallets([testWalletModel])
        tryCompare(page.walletViews, "count", 1)
        verify(destroyed)
        verify(reviewDestroyed)
        compare(page.viewForWallet(testSecondWalletModel), null)
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

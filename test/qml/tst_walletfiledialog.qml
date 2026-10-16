// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.2
import "../../qml/pages/settings"

TestCase {
    id: testCase
    name: "WalletFileDialog"
    when: windowShown
    width: 900
    height: 700
    Component { id: settings; WalletSectionPage {} }
    function init() {
        walletController.setSelectedWalletObject(testWalletModel)
        testWalletModel.resetWalletSettingsTestState()
    }
    function cleanup() { walletController.setSelectedWalletObject(testWalletModel) }
    function createPage() {
        const page = createTemporaryObject(settings, testCase.Window.window.contentItem)
        verify(page !== null)
        page.width = width
        page.height = height
        page.visible = true
        return page
    }
    function test_backup_rejects_old_acceptance_after_switching_back() {
        const page = createPage()
        page.startBackup()
        const dialog = findChild(page, "walletBackupDialog")
        verify(dialog.visible)
        walletController.setSelectedWalletObject(null)
        walletController.setSelectedWalletObject(testWalletModel)
        verify(!dialog.visible)
        dialog.selectAndAccept("file:///tmp/existing-A.bak")
        compare(testWalletModel.backupWalletCalls, 0)
        page.startBackup()
        dialog.selectAndAccept("file:///tmp/fresh-A.bak")
        compare(testWalletModel.backupWalletCalls, 1)
        compare(testWalletModel.lastBackupPath, "file:///tmp/fresh-A.bak")
        dialog.selectAndAccept("file:///tmp/duplicate.bak")
        compare(testWalletModel.backupWalletCalls, 1)
    }
    function test_backup_uses_exact_approved_destination() {
        const page = createPage()
        page.startBackup()
        findChild(page, "walletBackupDialog").selectAndAccept("file:///tmp/approved.custom")
        compare(testWalletModel.lastBackupPath, "file:///tmp/approved.custom")
    }
    function test_hidden_owner_cancels_backup() {
        const page = createPage()
        page.startBackup()
        const dialog = findChild(page, "walletBackupDialog")
        page.visible = false
        dialog.selectAndAccept("file:///tmp/hidden.bak")
        compare(testWalletModel.backupWalletCalls, 0)
    }
}

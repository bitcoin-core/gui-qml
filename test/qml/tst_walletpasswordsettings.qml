// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.2
import "../../qml/controls"
import "../../qml/pages/wallet"
import "../../qml/pages/settings" as SettingsPages

TestCase {
    function init() {
        testSetup.failOnWarnings()
    }

    name: "WalletPasswordSettings"
    when: windowShown
    width: 520
    height: 720

    Window {
        id: testWindow
        visible: true
        width: 520
        height: 720

        Item {
            id: testRoot
            anchors.fill: parent
            focus: true

            Loader {
                id: pageLoader
                anchors.fill: parent
            }
        }
    }

    Component {
        id: setPasswordComponent

        WalletPasswordSettings {
            updating: false
            wallet: testWalletModel
            width: 460
            height: 680
        }
    }

    Component {
        id: updatePasswordComponent

        WalletPasswordSettings {
            updating: true
            wallet: testWalletModel
            width: 460
            height: 680
        }
    }

    Component {
        id: walletSettingsComponent
        SettingsPages.WalletSectionPage {
            wallet: testWalletModel
            width: 460
            height: 680
        }
    }

    function cleanup() {
        pageLoader.sourceComponent = null
        testWalletModel.isEncrypted = false
        testWalletModel.resetWalletSettingsTestState()
        testSecondWalletModel.resetWalletSettingsTestState()
    }

    function test_set_password_explains_older_backup_coverage() {
        const page = createWalletPasswordSettingsPage(setPasswordComponent)
        const intro = findChild(page, "walletPasswordIntroTextDescription")
        verify(intro.text.indexOf("receiving and change keys") !== -1)
        verify(intro.text.indexOf("earlier backups") !== -1)
        verify(intro.text.indexOf("fresh backup") !== -1)
    }

    function test_encrypted_wallet_keeps_recovery_guidance_and_backup_action() {
        testWalletModel.isEncrypted = true
        pageLoader.sourceComponent = walletSettingsComponent
        const page = pageLoader.item
        verify(page !== null)
        const action = findChild(page, "walletBackupRow")
        verify(action !== null)
        compare(action.visible, true)
        verify(action.description.indexOf("earlier backups") !== -1)
        verify(action.description.indexOf("Keep your older backups") !== -1)
        const path = findChild(page, "walletSettingsBackupPathField")
        path.text = "/tmp/recovery-fresh.bak"
        action.clicked()
        compare(testWalletModel.backupWalletCalls, 1)
        compare(testWalletModel.lastBackupPath, "/tmp/recovery-fresh.bak")
        testWalletModel.isEncrypted = false
        compare(action.description, "")
        page.wallet = null
        compare(action.visible, false)
        compare(action.description, "")
    }

    function test_backup_confirmation_data() {
        return [ { tag: "new-final-name", replacing: false, approve: true },
                 { tag: "replace-approved", replacing: true, approve: true },
                 { tag: "replace-declined", replacing: true, approve: false },
                 { tag: "escape-declined", replacing: true, approve: false, escape: true } ]
    }

    function test_backup_confirmation(data) {
        pageLoader.sourceComponent = walletSettingsComponent
        testWalletModel.prepareBackupConfirmationTestState("/tmp/final <wallet>.bak", data.replacing)
        startBackup(pageLoader.item, "/tmp/final.bak")
        const popup = findChild(pageLoader.item, "walletBackupOverwritePopup")
        verify(popup !== null)
        tryCompare(popup, "visible", true)
        compare(popup.title, data.replacing ? "Replace backup?" : "Save backup?")
        const path = findChild(popup, "walletBackupFinalPathText")
        compare(path.text, "/tmp/final <wallet>.bak")
        compare(path.textFormat, Text.PlainText)
        const action = findChild(pageLoader.item, "walletBackupRow")
        compare(action.enabled, false)
        const button = testSetup.findVisualDescendant(popup.contentItem, data.approve ? "walletBackupOverwriteApproveButton" : "walletBackupOverwriteRejectButton")
        if (data.escape) {
            testWindow.requestActivate()
            tryCompare(testWindow, "active", true)
            popup.contentItem.forceActiveFocus()
            tryCompare(popup.contentItem, "activeFocus", true)
            keyClick(Qt.Key_Escape)
        } else {
            button.clicked()
        }
        tryCompare(popup, "visible", false)
        compare(action.enabled, true)
        compare(findChild(pageLoader.item, "walletBackupController").busy, false)
        compare(action.supportingText, data.approve ? "Backup saved." : "")
    }

    function test_backup_confirmation_stays_inside_window_for_long_path() {
        pageLoader.sourceComponent = walletSettingsComponent
        const longPath = "/tmp/" + Array(160).join("very-long-backup-folder-name/") + "wallet.bak"
        testWalletModel.prepareBackupConfirmationTestState(longPath, true)
        startBackup(pageLoader.item, longPath)
        const popup = findChild(pageLoader.item, "walletBackupOverwritePopup")
        verify(popup !== null)
        tryCompare(popup, "opened", true)
        const overlay = popup.parent
        const pathArea = findChild(popup, "walletBackupFinalPathTextArea")
        // The path alone is taller than the window; the popup must not be.
        verify(pathArea.contentHeight > overlay.height)
        tryVerify(function() { return popup.y >= 0 && popup.y + popup.height <= overlay.height },
            5000, "popup " + popup.y + "+" + popup.height + " exceeds " + overlay.height)
        verify(pathArea.interactive)
        pathArea.contentY = pathArea.contentHeight - pathArea.height
        compare(findChild(popup, "walletBackupFinalPathText").text, longPath)
        const button = testSetup.findVisualDescendant(popup.contentItem, "walletBackupOverwriteApproveButton")
        verify(button !== null)
        const bottom = button.mapToItem(overlay, 0, button.height).y
        verify(bottom <= overlay.height, "approve button bottom " + bottom + " exceeds " + overlay.height)
        verify(waitForRendering(button))
        mouseClick(button)
        tryCompare(popup, "visible", false)
        compare(findChild(pageLoader.item, "walletBackupController").busy, false)
        compare(findChild(pageLoader.item, "walletBackupRow").supportingText, "Backup saved.")
    }

    // Starts a backup from the page as its row does, with the automation path.
    function startBackup(page, path) {
        findChild(page, "walletSettingsBackupPathField").text = path
        findChild(page, "walletBackupRow").clicked()
    }

    function createWalletPasswordSettingsPage(component) {
        pageLoader.sourceComponent = component
        const page = pageLoader.item
        verify(page !== null)
        testWindow.requestActivate()
        tryCompare(testWindow, "active", true)
        testRoot.forceActiveFocus()
        page.focusInitialPasswordField()
        wait(0)
        return page
    }

    function typeText(text) {
        for (let i = 0; i < text.length; ++i) {
            keyClick(text.charAt(i))
        }
    }

    function test_set_password_focuses_new_password_field() {
        const page = createWalletPasswordSettingsPage(setPasswordComponent)
        const currentPassword = findChild(page, "walletPasswordCurrentField")
        const newPassword = findChild(page, "walletPasswordNewField")
        const intro = findChild(page, "walletPasswordIntroTextDescription")
        const newEntry = findChild(page, "walletPasswordNewEntry")
        const formSection = findChild(page, "walletPasswordFormSection")
        const formCard = findChild(page, "walletPasswordFormSectionCard")
        const saveButton = findChild(page, "walletPasswordSaveButton")
        verify(currentPassword !== null)
        verify(newPassword !== null)
        verify(intro !== null)
        verify(newEntry !== null)
        verify(formSection !== null)
        verify(formCard !== null)
        verify(saveButton !== null)

        compare(page.updating, false)
        compare(page.maximumContentWidth, 640)
        verify(intro.text.indexOf("password encrypts the wallet") !== -1)
        compare(formCard.radius, 16)
        compare(newPassword.background.color, Theme.color.surface2)
        compare(currentPassword.focus, false)
        compare(newPassword.focus, true)
        compare(newPassword.echoMode, TextInput.Password)

        typeText("newsecret")
        compare(currentPassword.text, "")
        compare(newPassword.text, "newsecret")
    }

    // Reports the sizes behind a check box's height, so a failure on another
    // Qt version shows where the wrapped label's height was lost.
    function sizes(checkBox) {
        const content = checkBox.contentItem
        const label = content.children[0]
        const column = checkBox.parent
        return "box " + checkBox.width + "x" + checkBox.height
            + " implicit " + checkBox.implicitHeight + " content implicit " + checkBox.implicitContentHeight
            + "; contentItem " + content.width + "x" + content.height + " implicit " + content.implicitHeight
            + "; label " + label.width + "x" + label.height + " implicit " + label.implicitHeight
            + " lines " + label.lineCount
            + "; column " + column.width + "x" + column.height + " implicit " + column.implicitHeight
    }

    function test_wrapped_acknowledgement_keeps_clear_of_the_save_button() {
        const originalScale = Theme.textScale
        try {
            for (const scale of [1, 1.3]) {
                Theme.textScale = scale
                const page = createWalletPasswordSettingsPage(setPasswordComponent)
                const acknowledgement = findChild(page, "walletPasswordConfirmToggle")
                const saveButton = findChild(page, "walletPasswordSaveButton")
                const fits = function() { return acknowledgement.height >= acknowledgement.contentItem.implicitHeight }
                for (let i = 0; i < 50 && !fits(); ++i) wait(20)
                verify(fits(), "scale " + scale + ": " + sizes(acknowledgement))
                verify(acknowledgement.contentItem.implicitHeight > Theme.size.controlSmall, "scale " + scale)
                const bottom = acknowledgement.mapToItem(page, 0, acknowledgement.height).y
                tryVerify(function() { return saveButton.mapToItem(page, 0, 0).y >= bottom }, 1000, "scale " + scale)
                pageLoader.sourceComponent = undefined
            }
        } finally {
            Theme.textScale = originalScale
        }
    }

    // #1132: the new passphrase drops a pasted line ending; the current one,
    // which must match what the wallet was encrypted with, keeps it.
    function test_update_password_drops_pasted_line_endings_only_from_the_new_passphrase() {
        const page = createWalletPasswordSettingsPage(updatePasswordComponent)
        const current = findChild(page, "walletPasswordCurrentField")
        const fresh = findChild(page, "walletPasswordNewField")
        const confirm = findChild(page, "walletPasswordConfirmField")
        current.insert(0, "old secret\n")
        fresh.insert(0, "new secret\n")
        confirm.insert(0, "new secret\r\n")
        compare(current.text, "old secret\n")
        compare(fresh.text, "new secret")
        compare(confirm.text, "new secret")
    }

    function test_update_password_focuses_current_password_field() {
        const page = createWalletPasswordSettingsPage(updatePasswordComponent)
        const currentPassword = findChild(page, "walletPasswordCurrentField")
        const newPassword = findChild(page, "walletPasswordNewField")
        const currentEntry = findChild(page, "walletPasswordCurrentEntry")
        const currentLabel = findChild(page, "walletPasswordCurrentLabel")
        const currentToggle = findChild(page, "walletPasswordCurrentVisibilityToggle")
        verify(currentPassword !== null)
        verify(newPassword !== null)
        verify(currentEntry !== null)
        verify(currentLabel !== null)
        verify(currentToggle !== null)

        compare(page.updating, true)
        compare(currentLabel.text, "Current password")
        compare(currentPassword.focus, true)
        compare(newPassword.focus, false)
        compare(currentPassword.echoMode, TextInput.Password)

        currentToggle.clicked()
        compare(currentEntry.passwordVisible, true)
        compare(currentPassword.echoMode, TextInput.Normal)

        typeText("currentsecret")
        compare(currentPassword.text, "currentsecret")
        compare(newPassword.text, "")
    }

    function startPasswordChange() {
        const page = createWalletPasswordSettingsPage(updatePasswordComponent)
        findChild(page, "walletPasswordCurrentField").text = "current secret"
        findChild(page, "walletPasswordNewField").text = "replacement secret"
        findChild(page, "walletPasswordConfirmField").text = "replacement secret"
        const save = findChild(page, "walletPasswordSaveButton")
        compare(save.enabled, true)
        save.clicked()
        compare(testWalletModel.securityBusy, true)
        compare(page.waitingForSave, true)
        compare(findChild(page, "walletPasswordController").operation, testWalletModel.security.activeOperation)
        compare(save.enabled, false)
        compare(findChild(page, "walletPasswordCurrentField").enabled, false)
        compare(page.successText, "")
        return page
    }

    function test_password_admission_waits_for_success() {
        const page = startPasswordChange()
        compare(findChild(page, "walletPasswordCurrentField").text, "current secret")
        testWalletModel.finishSecurityForTest(true)
        compare(page.waitingForSave, false)
        compare(page.successText, "Password updated successfully.")
        compare(findChild(page, "walletPasswordCurrentField").text, "")
        compare(findChild(page, "walletPasswordNewField").text, "")
        compare(findChild(page, "walletPasswordConfirmField").text, "")
    }

    function test_password_failure_preserves_active_recovery_fields() {
        const page = startPasswordChange()
        testWalletModel.finishSecurityForTest(false)
        compare(page.waitingForSave, false)
        compare(page.successText, "")
        compare(page.errorText, "Security operation failed")
        compare(findChild(page, "walletPasswordCurrentField").text, "current secret")
        compare(findChild(page, "walletPasswordNewField").text, "replacement secret")
        compare(findChild(page, "walletPasswordSaveButton").enabled, true)
    }

    function test_late_password_result_does_not_retarget_data() {
        return [ {tag: "hidden"}, {tag: "switched"}, {tag: "unloaded"} ]
    }

    function test_late_password_result_does_not_retarget(data) {
        const page = startPasswordChange()
        if (data.tag === "hidden") page.visible = false
        else if (data.tag === "switched") {
            // A page never changes wallet; one that does disables its controller.
            ignoreWarning(new RegExp("WalletPasswordController was given a second wallet"))
            page.wallet = testSecondWalletModel
        }
        else testWalletModel.available = false
        compare(page.waitingForSave, false)
        compare(findChild(page, "walletPasswordCurrentField").text, "")
        compare(findChild(page, "walletPasswordNewField").text, "")
        compare(findChild(page, "walletPasswordConfirmField").text, "")
        // The original owned operation still completes, but this view has
        // detached. A new wallet or a later activation receives no old result.
        testWalletModel.finishSecurityForTest(true)
        if (data.tag === "hidden") page.visible = true
        compare(page.successText, "")
        compare(page.errorText, "")
        compare(testSecondWalletModel.securityBusy, false)
    }

    function test_update_password_result_uses_toast_without_leaving_page() {
        const page = createWalletPasswordSettingsPage(updatePasswordComponent)
        const banner = findChild(page, "walletPasswordResultBanner")
        const resultText = findChild(page, "walletPasswordErrorText")
        verify(banner !== null)
        verify(resultText !== null)
        compare(banner.visible, false)

        let savedCount = 0
        page.saved.connect(function() { savedCount += 1 })

        findChild(page, "walletPasswordCurrentField").text = "current secret"
        findChild(page, "walletPasswordNewField").text = "replacement secret"
        findChild(page, "walletPasswordConfirmField").text = "replacement secret"
        findChild(page, "walletPasswordSaveButton").clicked()
        testWalletModel.finishSecurityForTest(false)
        tryCompare(banner, "visible", true)
        compare(resultText.text, "Security operation failed")
        compare(banner.iconSource.toString(), "image://images/alert-filled")
        compare(banner.textColor, Theme.color.dangerText)

        findChild(page, "walletPasswordController").clearResult()
        findChild(page, "walletPasswordSaveButton").clicked()
        testWalletModel.finishSecurityForTest(true)
        compare(savedCount, 0)
        compare(page.successText, "Password updated successfully.")
        tryCompare(banner, "visible", true)
        compare(resultText.text, "Password updated successfully.")
        compare(banner.iconSource.toString(), "image://images/check")
        compare(banner.textColor, Theme.color.successText)
    }
}

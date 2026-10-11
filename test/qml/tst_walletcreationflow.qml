// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtTest 1.2
import "../../qml/controls"
import "../../qml/components"
import "../../qml/pages/wallet"

TestCase {
    name: "WalletCreationFlow"
    when: windowShown
    visible: true
    width: 800
    height: 665

    readonly property string validXpub:
        "xpub661MyMwAqRbcFtXgS5sYJABqqG9YLmC4Q1Rdap9gSE8NqtwybGhePY2gZ29ESFjqJoCu1Rupje8YtGqsefD265TMg7usUDFdp6W1EGMcet8"

    Component {
        id: flowComponent
        WalletCreationFlow { width: 720; height: 660; modalView: true }
    }

    Component {
        id: modalComponent
        WalletCreationModal { }
    }

    function init() {
        walletController.reset()
        walletController.initialized = true
    }

    function createFlow() {
        const flow = createTemporaryObject(flowComponent, this)
        verify(flow !== null)
        return flow
    }

    function tabToItem(item, modifiers) {
        for (let i = 0; i < 20 && !item.activeFocus; ++i) {
            keyClick(Qt.Key_Tab, modifiers)
        }
        verify(item.activeFocus, "Keyboard navigation must reach " + item.objectName)
    }

    function verifyItemInViewport(item, viewport) {
        tryVerify(function() {
            const position = item.mapToItem(viewport, 0, 0)
            return position.y >= 8 && position.y + item.height <= viewport.height - 8
        }, 1000, "Focused control and its focus outline must be visible")
    }

    function test_keyboard_navigation_scrolls_wallet_form_at_minimum_window_size() {
        const modal = createTemporaryObject(modalComponent, this)
        verify(modal !== null)
        modal.open()
        tryCompare(modal, "opened", true)
        compare(modal.width, 768)
        compare(modal.height, 625)
        mouseClick(findChild(modal, "walletTypeRegular"))
        tryCompare(modal.flow.currentItem, "objectName", "createWalletFormPage")
        tryCompare(modal.flow, "busy", false)
        const form = modal.flow.currentItem
        const name = findChild(form, "createWalletNameInput")
        const password = findChild(form, "createWalletPasswordInput")
        const confirm = findChild(form, "createWalletPasswordRepeatInput")
        const acknowledgement = findChild(form, "createWalletPasswordConfirmToggle")
        mouseClick(name)
        verify(name.activeFocus)
        verifyItemInViewport(name, form.scrollView)
        tabToItem(password, Qt.NoModifier)
        verifyItemInViewport(password, form.scrollView)
        tabToItem(confirm, Qt.NoModifier)
        verifyItemInViewport(confirm, form.scrollView)
        tabToItem(acknowledgement, Qt.NoModifier)
        verifyItemInViewport(acknowledgement, form.scrollView)
        verify(form.scrollView.contentItem.contentY > 0)
        tabToItem(confirm, Qt.ShiftModifier)
        verifyItemInViewport(confirm, form.scrollView)
        tabToItem(password, Qt.ShiftModifier)
        verifyItemInViewport(password, form.scrollView)
        tabToItem(name, Qt.ShiftModifier)
        verifyItemInViewport(name, form.scrollView)
        modal.close()
        tryCompare(modal, "visible", false)
    }

    function test_watch_only_validates_key_and_finishes_on_ready_page() {
        const flow = createFlow()
        findChild(flow, "walletTypeViewOnly").clicked()
        tryVerify(function() { return findChild(flow, "createWalletFormPage") !== null })

        const form = findChild(flow, "createWalletFormPage")
        verify(findChild(form, "onboardingScrollView") !== null)
        compare(form.showBackButton, true)
        compare(form.title, "")
        compare(form.heading, "View-only wallet")
        compare(findChild(form, "watchOnlyXpubPasteButton"), null)
        const name = findChild(form, "createWalletNameInput")
        const xpub = findChild(form, "watchOnlyXpubInput")
        const xpubEntry = findChild(form, "createWalletXpubEntry")
        const initialHeight = xpubEntry.height
        xpub.text = "line\nline\nline\nline\nline\nline"
        tryVerify(function() { return xpubEntry.height > initialHeight })
        name.text = "Watch wallet"
        xpub.text = "invalid"
        compare(form.canCreate, false)
        xpub.text = validXpub
        compare(form.canCreate, true)

        findChild(form, "createWalletFormCreateButton").clicked()
        tryVerify(function() { return findChild(flow, "walletCreationReadyPage") !== null })
        const ready = findChild(flow, "walletCreationReadyPage")
        compare(ready.backupRequired, false)
        compare(ready.loadedChildView, null)
        compare(ready.showBackButton, false)
        compare(ready.primaryButton.visible, false)
        compare(ready.secondaryButton, findChild(ready, "createWalletReadyDoneButton"))
        compare(ready.secondaryButton.buttonSize, NeutralButton.Large)
        let finished = false
        flow.finished.connect(function(openActivity) { finished = openActivity })
        findChild(ready, "createWalletReadyDoneButton").clicked()
        compare(finished, true)
    }

    function test_external_signer_uses_onboarding_form_and_ready_page() {
        walletController.canCreateExternalSignerWallet = true
        walletController.externalSignerName = "trezor_t"
        walletController.suggestedExternalSignerWalletName = "trezor_t2"
        const flow = createFlow()
        findChild(flow, "walletTypeExternalSigner").clicked()
        tryVerify(function() { return findChild(flow, "externalSignerWalletFormPage") !== null })

        const form = findChild(flow, "externalSignerWalletFormPage")
        compare(form.heading, "External signer wallet")
        compare(form.subheading, "Connected signer: trezor_t")
        compare(form.title, "")
        compare(findChild(form, "externalWalletNameInput").text, "trezor_t2")
        compare(form.primaryButton, findChild(form, "createExternalWalletButton"))
        compare(form.primaryButton.enabled, true)
        form.primaryButton.clicked()

        tryVerify(function() { return findChild(flow, "externalWalletCreatedPage") !== null })
        const ready = findChild(flow, "externalWalletCreatedPage")
        verify(findChild(ready, "externalWalletSuccessBadge") !== null)
        compare(ready.showBackButton, false)
        compare(ready.primaryButton.visible, false)
        compare(ready.secondaryButton.buttonSize, NeutralButton.Large)
        let finished = false
        flow.finished.connect(function(openActivity) { finished = openActivity })
        findChild(ready, "externalWalletCreatedDoneButton").clicked()
        compare(finished, true)
    }

    function test_regular_requires_acknowledgement_and_warns_for_unencrypted_creation() {
        const flow = createFlow()
        findChild(flow, "walletTypeRegular").clicked()
        tryVerify(function() { return findChild(flow, "createWalletFormPage") !== null })
        const form = findChild(flow, "createWalletFormPage")
        verify(form.scrollView.contentHeight > form.scrollView.height)
        const create = findChild(form, "createWalletFormCreateButton")
        const encrypt = findChild(form, "createWalletEncryptCheckBox")
        const password = findChild(form, "createWalletPasswordInput")
        const confirm = findChild(form, "createWalletPasswordRepeatInput")
        const warning = findChild(form, "createWalletUnencryptedWarning")
        const acknowledgement = findChild(form, "createWalletPasswordConfirmToggle")
        const unencryptedAcknowledgement = findChild(form, "createWalletUnencryptedConfirmToggle")

        compare(encrypt.checked, true)
        compare(form.title, "")
        compare(unencryptedAcknowledgement.visible, false)
        compare(form.heading, "Single-key wallet")
        compare(findChild(form, "createWalletNameEntry").supportingText,
            "You cannot change this later.")
        compare(encrypt.text, "Encrypt this wallet with a strong password.")
        compare(acknowledgement.loadedTrailingItem.checked, false)
        findChild(form, "createWalletNameInput").text = "Regular wallet"
        password.text = "correct horse battery staple"
        confirm.text = "different"
        compare(form.canCreate, false)
        confirm.text = password.text
        compare(form.canCreate, false)
        acknowledgement.clicked()
        compare(acknowledgement.loadedTrailingItem.checked, true)
        compare(form.canCreate, true)
        encrypt.checked = false
        compare(acknowledgement.loadedTrailingItem.checked, false)
        compare(unencryptedAcknowledgement.visible, true)
        compare(unencryptedAcknowledgement.loadedTrailingItem.checked, false)
        compare(form.canCreate, false)
        compare(create.enabled, false)
        unencryptedAcknowledgement.clicked()
        compare(unencryptedAcknowledgement.loadedTrailingItem.checked, true)
        compare(form.canCreate, true)
        encrypt.checked = true
        encrypt.checked = false
        compare(unencryptedAcknowledgement.loadedTrailingItem.checked, false)
        compare(form.canCreate, false)
        unencryptedAcknowledgement.clicked()
        create.clicked()
        verify(warning.message.indexOf("won't be able to set a password later") !== -1)
        compare(warning.visibleActions[1].role, 0)
        warning.close()
        warning.visibleActions[1].triggered()
        tryVerify(function() { return findChild(flow, "walletCreationReadyPage") !== null })
        const ready = findChild(flow, "walletCreationReadyPage")
        compare(ready.watchOnly, false)
        compare(ready.backupRequired, true)
        compare(ready.encryptedWallet, false)
    }

    function test_encrypted_ready_keeps_password_backup_guidance() {
        const flow = createFlow()
        findChild(flow, "walletTypeRegular").clicked()
        tryVerify(function() { return findChild(flow, "createWalletFormPage") !== null })
        const form = findChild(flow, "createWalletFormPage")
        findChild(form, "createWalletNameInput").text = "Encrypted wallet"
        findChild(form, "createWalletPasswordInput").text = "correct horse battery staple"
        findChild(form, "createWalletPasswordRepeatInput").text = "correct horse battery staple"
        compare(form.canCreate, false)
        findChild(form, "createWalletPasswordConfirmToggle").clicked()
        compare(form.canCreate, true)
        let warningOpened = false
        const warning = findChild(form, "createWalletUnencryptedWarning")
        warning.visibleChanged.connect(function() {
            if (warning.visible) warningOpened = true
        })
        findChild(form, "createWalletFormCreateButton").clicked()
        tryVerify(function() { return findChild(flow, "walletCreationReadyPage") !== null })
        compare(warningOpened, false)
        const ready = findChild(flow, "walletCreationReadyPage")
        compare(ready.encryptedWallet, true)
        verify(findChild(ready, "walletCreationBackupSection").description.indexOf("password") !== -1)
    }

    function test_import_choice_uses_onboarding_error_and_complete_pages() {
        const flow = createFlow()
        flow.importingWallet = true
        walletController.walletLoadError = "Data is not in recognized format."
        tryVerify(function() { return findChild(flow, "walletImportErrorPage") !== null })
        const errorPage = findChild(flow, "walletImportErrorPage")
        compare(findChild(errorPage, "importWalletErrorView").visible, true)
        compare(findChild(errorPage, "importWalletErrorTitle").text,
            walletController.walletImportErrorTitle)
        compare(findChild(errorPage, "importWalletErrorDescription").text,
            walletController.walletImportErrorDescription)

        walletController.lastImportedWalletName = "Imported wallet"
        walletController.lastImportedWalletKeyScheme = "Single-key"
        findChild(flow, "importWalletPathField").text = "/tmp/test-wallet.bak"
        findChild(errorPage, "importWalletChooseAnotherFileButton").clicked()
        tryVerify(function() { return findChild(flow, "importWalletSuccessPage") !== null })
        const complete = findChild(flow, "importWalletSuccessPage")
        compare(complete.heading, "Import complete")
        verify(findChild(complete, "importWalletSuccessBadge") !== null)
        const infoSection = findChild(complete, "importWalletInfoSection")
        compare(infoSection.showBackground, true)
        compare(infoSection.isOnSurface, true)
        compare(infoSection.showGradientBorder, false)
        compare(findChild(complete, "importWalletSuccessWalletName").text, "Imported wallet")
        compare(findChild(complete, "importWalletSuccessKeyScheme").text, "Single-key")
        compare(complete.primaryButton.visible, false)
        compare(complete.secondaryButton.buttonSize, NeutralButton.Large)
        let finished = false
        flow.finished.connect(function(openActivity) { finished = openActivity })
        findChild(complete, "importWalletSuccessOverviewButton").clicked()
        compare(finished, true)
    }

    function test_import_choice_uses_selected_file() {
        const flow = createFlow()
        findChild(flow, "importWalletPathField").text = "/tmp/test-wallet.bak"
        findChild(flow, "walletTypeImport").clicked()
        tryVerify(function() { return findChild(flow, "importWalletSuccessPage") !== null })
        compare(findChild(flow, "importWalletPathField").text, "")
        compare(findChild(flow, "walletImportErrorPage"), null)
        compare(flow.importingWallet, false)
    }

    function test_import_file_dialog_selection_imports_file() {
        const flow = createFlow()
        const dialog = findChild(flow, "walletImportFileDialog")
        verify(dialog !== null)
        findChild(flow, "walletTypeImport").clicked()
        compare(dialog.openCalls, 1)
        dialog.selectAndAccept("file:///tmp/test-wallet.bak")
        tryVerify(function() { return findChild(flow, "importWalletSuccessPage") !== null })
        compare(findChild(flow, "walletImportErrorPage"), null)
        compare(flow.importingWallet, false)
    }

    function test_import_file_dialog_rejection_keeps_wallet_types() {
        const flow = createFlow()
        const dialog = findChild(flow, "walletImportFileDialog")
        verify(dialog !== null)
        verify(dialog.selectedFile !== undefined)
        compare(dialog.nameFilters.length, 2)
        dialog.rejected()
        compare(flow.depth, 1)
        compare(flow.currentItem.objectName, "walletCreationTypePage")
        compare(flow.importingWallet, false)
        compare(findChild(flow, "walletImportErrorPage"), null)
    }

    function test_import_file_dialog_acceptance_without_selection_does_not_import() {
        const flow = createFlow()
        const dialog = findChild(flow, "walletImportFileDialog")
        verify(dialog !== null)
        compare(dialog.selectedFile.toString(), "")
        dialog.accepted()
        compare(flow.depth, 1)
        compare(flow.currentItem.objectName, "walletCreationTypePage")
        compare(flow.importingWallet, false)
        compare(findChild(flow, "importWalletSuccessPage"), null)
    }
}

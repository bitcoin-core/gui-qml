// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Window 2.15
import QtTest 1.2
import org.bitcoincore.qt 1.0
import "../../qml/controls"
import "../../qml/components"
import "../../qml/pages/wallet"

TestCase {
    id: testCase
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

    Component {
        id: boundedCreateFieldsComponent
        CreateWalletFields { width: 700; watchOnly: false; nameError: ""; xpubChecked: true; validXpub: true }
    }

    Component {
        id: boundedExternalFieldsComponent
        ExternalSignerWalletFields { width: 700; defaultWalletName: "Signer"; nameError: ""; creatingWallet: false }
    }

    function init() {
        testSetup.failOnWarnings()
        walletController.reset()
        walletController.initialized = true
        walletController.setSelectedWalletObject(testWalletModel)
    }

    property real savedWindowWidth: 0
    property real savedWindowHeight: 0
    function cleanup() {
        if (savedWindowWidth > 0) {
            testCase.Window.window.width = savedWindowWidth
            testCase.Window.window.height = savedWindowHeight
            savedWindowWidth = 0
        }
    }
    function createInputFlow(temporary) {
        const window = testCase.Window.window
        verify(window !== null)
        savedWindowWidth = window.width
        savedWindowHeight = window.height
        window.width = Math.max(window.width, 800)
        window.height = Math.max(window.height, 720)
        const flow = temporary === false ? flowComponent.createObject(this) : createTemporaryObject(flowComponent, this)
        verify(flow !== null)
        return flow
    }
    function clickVisible(item) {
        verify(item !== null)
        // StackView deliberately filters child pointer events during a
        // transition, even once the incoming button is visible and enabled.
        for (let ancestor = item.parent; ancestor; ancestor = ancestor.parent) {
            if (ancestor.objectName === "walletCreationPageStack") tryCompare(ancestor, "busy", false)
        }
        tryVerify(function() {
            if (!item.visible || !item.enabled || item.width <= 0 || item.height <= 0) return false
            const point = item.mapToItem(testCase.Window.window.contentItem, item.width / 2, item.height / 2)
            if (point.x < 0 || point.y < 0 || point.x >= testCase.Window.window.width || point.y >= testCase.Window.window.height) return false
            for (let ancestor = item.parent; ancestor; ancestor = ancestor.parent) {
                if (!ancestor.visible) return false
                if (ancestor.clip) {
                    const center = item.mapToItem(ancestor, item.width / 2, item.height / 2)
                    if (center.x < 0 || center.y < 0 || center.x >= ancestor.width || center.y >= ancestor.height) return false
                }
            }
            return true
        }, 5000, "The physical click center must be visible through every ancestor clip")
        mouseClick(item)
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

    function test_live_form_edit_cannot_end_the_accepted_creation_data() {
        return [{tag: "software", watchOnly: false}, {tag: "watch-only", watchOnly: true}]
    }
    function test_live_form_edit_cannot_end_the_accepted_creation(data) {
        walletController.holdCreation = true
        const flow = createInputFlow()
        findChild(flow, data.watchOnly ? "walletTypeViewOnly" : "walletTypeRegular").clicked()
        tryCompare(flow.currentItem, "objectName", "createWalletFormPage")
        const form = flow.currentItem
        const name = findChild(form, "createWalletNameInput")
        testSetup.replaceText(name, "Original submitted wallet")
        if (data.watchOnly) testSetup.replaceText(findChild(form, "watchOnlyXpubInput"), validXpub)
        else {
            testSetup.replaceText(findChild(form, "createWalletPasswordInput"), "original password")
            testSetup.replaceText(findChild(form, "createWalletPasswordRepeatInput"), "original password")
            findChild(form, "createWalletPasswordConfirmToggle").toggle()
        }
        tryCompare(form, "canCreate", true)
        clickVisible(findChild(form, "createWalletFormCreateButton"))
        tryCompare(walletController, "creationCalls", 1)
        compare(walletController.submittedCreationName, "Original submitted wallet")
        verify(walletController.walletLoadInProgress)
        const original = flow.operation
        verify(original !== null)
        testSetup.replaceText(name, "A later live draft")
        verify(walletController.walletLoadInProgress)
        compare(flow.operation, original)
        compare(original.submittedName, "Original submitted wallet")
        compare(form.canCreate, false)
        tryCompare(walletController, "creationCalls", 1)
        walletController.finishCreation()
        tryCompare(flow.currentItem, "objectName", "walletCreationReadyPage")
        compare(walletController.walletLoadInProgress, false)
        compare(flow.operation, original)
        compare(original.phase, WalletOperationModel.Succeeded)
        tryCompare(walletController, "creationCalls", 1)
    }
    function test_transport_refusal_is_scoped_and_creates_nothing() {
        walletController.refuseNextCreation = true
        const flow = createInputFlow()
        findChild(flow, "walletTypeRegular").clicked()
        tryCompare(flow.currentItem, "objectName", "createWalletFormPage")
        const form = flow.currentItem
        testSetup.replaceText(findChild(form, "createWalletNameInput"), "Refused request")
        testSetup.replaceText(findChild(form, "createWalletPasswordInput"), "original password")
        testSetup.replaceText(findChild(form, "createWalletPasswordRepeatInput"), "original password")
        findChild(form, "createWalletPasswordConfirmToggle").toggle()
        tryCompare(form, "canCreate", true)
        clickVisible(findChild(form, "createWalletFormCreateButton"))
        tryVerify(function() { return flow.operation !== null })
        compare(flow.operation.phase, WalletOperationModel.Rejected)
        verify(findChild(form, "createWalletFormErrorText").text.indexOf("No wallet was created") !== -1)
        compare(walletController.creationCalls, 0)
        compare(walletController.walletLoadInProgress, false)
        compare(findChild(flow, "walletCreationReadyPage"), null)
    }

    function test_created_artifact_failure_has_recovery_without_success_or_recreation() {
        walletController.holdCreation = true
        const flow = createInputFlow()
        findChild(flow, "walletTypeRegular").clicked()
        tryCompare(flow.currentItem, "objectName", "createWalletFormPage")
        const form = flow.currentItem
        testSetup.replaceText(findChild(form, "createWalletNameInput"), "Original artifact")
        testSetup.replaceText(findChild(form, "createWalletPasswordInput"), "original password")
        testSetup.replaceText(findChild(form, "createWalletPasswordRepeatInput"), "original password")
        findChild(form, "createWalletPasswordConfirmToggle").toggle()
        tryCompare(form, "canCreate", true)
        clickVisible(findChild(form, "createWalletFormCreateButton"))
        const original = flow.operation
        testSetup.replaceText(findChild(form, "createWalletNameInput"), "Later draft")
        walletController.finishCreation("Wallet storage was created, but setup failed", true)
        tryCompare(flow.currentItem, "objectName", "walletCreationOutcomePage")
        compare(flow.operation, original)
        compare(original.phase, WalletOperationModel.Partial)
        compare(findChild(flow.currentItem, "walletCreationRecoveryPaths").text, "wallets/Original artifact")
        compare(findChild(flow, "walletCreationReadyPage"), null)
        compare(original.canOpenResult, false)
        compare(original.resultWallet, null)
        tryCompare(walletController, "creationCalls", 1)
        compare(walletController.walletLoadInProgress, false)
        let cancelled = false
        flow.finished.connect(function(openActivity, wallet) { cancelled = !openActivity && wallet === null })
        clickVisible(findChild(flow.currentItem, "walletCreationOutcomeDoneButton"))
        compare(cancelled, true)
        tryCompare(walletController, "creationCalls", 1)
    }

    function test_creation_warning_is_visible_without_changing_success() {
        const warning = "Ordinary fallbackfee warning for this result"
        walletController.creationWarnings = warning
        const flow = createFlow()
        const ready = createEncryptedWallet(flow)
        compare(flow.operation.phase, WalletOperationModel.Succeeded)
        const shown = findChild(ready, "walletCreationWarnings")
        verify(shown !== null)
        compare(shown.text, warning)
        compare(shown.visible, true)
        walletController.walletLoadWarnings = "Another operation's warning"
        walletController.walletLoadWarningsChanged()
        compare(shown.text, warning)
        compare(flow.operation.error.message, "")
    }
    function test_detaching_the_creation_view_keeps_the_operation_result() {
        walletController.holdCreation = true
        let flow = createInputFlow(false)
        try {
        verify(flow !== null)
        findChild(flow, "walletTypeRegular").clicked()
        tryCompare(flow.currentItem, "objectName", "createWalletFormPage")
        const form = flow.currentItem
        testSetup.replaceText(findChild(form, "createWalletNameInput"), "Detached creation")
        testSetup.replaceText(findChild(form, "createWalletPasswordInput"), "original password")
        testSetup.replaceText(findChild(form, "createWalletPasswordRepeatInput"), "original password")
        findChild(form, "createWalletPasswordConfirmToggle").toggle()
        tryCompare(form, "canCreate", true)
        clickVisible(findChild(form, "createWalletFormCreateButton"))
        const original = flow.operation
        verify(original !== null)
        flow.destroy()
        testSetup.processDeferredDeletes()
        flow = null
        verify(walletController.walletLoadInProgress)
        walletController.finishCreation()
        tryCompare(original, "phase", WalletOperationModel.Succeeded)
        compare(original.submittedName, "Detached creation")
        tryCompare(walletController, "creationCalls", 1)
        compare(original.resultWallet, testWalletModel)
        } finally {
            if (flow) flow.destroy()
        }
    }

    function test_recreated_view_projects_the_retained_terminal_result_without_signal_replay() {
        walletController.holdCreation = true
        let flow = createInputFlow(false)
        try {
            findChild(flow, "walletTypeViewOnly").clicked()
            tryCompare(flow.currentItem, "objectName", "createWalletFormPage")
            const form = flow.currentItem
            testSetup.replaceText(findChild(form, "createWalletNameInput"), "Original retained operation")
            testSetup.replaceText(findChild(form, "watchOnlyXpubInput"), validXpub)
            tryCompare(form, "canCreate", true)
            clickVisible(findChild(form, "createWalletFormCreateButton"))
            const original = flow.operation
            verify(original !== null)
            flow.visible = false
            flow.destroy()
            testSetup.processDeferredDeletes()
            flow = null
            walletController.finishCreation()
            tryCompare(original, "phase", WalletOperationModel.Succeeded)
            // The replacement subscribes after completion. Its initial read
            // carries the exact result; no success signal is emitted again.
            const replacement = createTemporaryObject(flowComponent, this, {operation: original})
            verify(replacement !== null)
            tryVerify(function() { return findChild(replacement, "walletCreationReadyPage") !== null })
            compare(replacement.operation, original)
            compare(replacement.completedWallet, original.resultWallet)
            compare(original.submittedName, "Original retained operation")
            compare(walletController.creationCalls, 1)
        } finally {
            if (flow) flow.destroy()
        }
    }

    function test_operation_errors_use_bounded_summaries_and_virtual_full_details() {
        const original = "Original operation error: " + "z".repeat(6000)
        const createFields = createTemporaryObject(boundedCreateFieldsComponent, this)
        verify(createFields !== null)
        walletController.walletCreateError = original
        walletController.walletCreateErrorChanged()
        const summary = findChild(createFields, "createWalletFormErrorText")
        compare(summary.text, original.slice(0, 1024))
        const details = findChild(createFields, "walletOperationErrorDetails")
        verify(details !== null)
        tryCompare(details, "count", Math.ceil(original.length / 1024))
        findChild(createFields, "walletOperationErrorDetailsButton").clicked()
        tryCompare(details, "visible", true)
        details.positionViewAtIndex(0, ListView.Beginning)
        tryVerify(function() { return details.itemAtIndex(0) !== null })
        compare(details.itemAtIndex(0).content, original.slice(0, 1024))
        details.positionViewAtEnd()
        tryVerify(function() { return details.itemAtIndex(details.count - 1) !== null })
        compare(details.itemAtIndex(details.count - 1).content, original.slice((details.count - 1) * 1024))

        const externalFields = createTemporaryObject(boundedExternalFieldsComponent, this)
        verify(externalFields !== null)
        walletController.walletLoadError = original
        walletController.walletLoadErrorChanged()
        compare(findChild(externalFields, "externalWalletNameError").text, original.slice(0, 1024))
        const externalDetails = findChild(externalFields, "walletOperationErrorDetails")
        tryCompare(externalDetails, "count", Math.ceil(original.length / 1024))
    }

    function test_keyboard_navigation_scrolls_wallet_form_at_minimum_window_size() {
        const modal = createTemporaryObject(modalComponent, this)
        verify(modal !== null)
        modal.open()
        tryCompare(modal, "opened", true)
        compare(modal.width, 760)
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
        compare(form.heading, "Watch-only wallet")
        compare(findChild(form, "watchOnlyXpubPasteButton"), null)
        const name = findChild(form, "createWalletNameInput")
        const xpub = findChild(form, "watchOnlyXpubInput")
        const xpubEntry = findChild(form, "createWalletXpubEntry")
        const initialHeight = xpubEntry.height
        xpub.text = "line\nline\nline\nline\nline\nline"
        tryVerify(function() { return xpubEntry.height > initialHeight })
        testSetup.replaceText(name, "Watch wallet")
        testSetup.replaceText(xpub, "invalid")
        compare(form.canCreate, false)
        testSetup.replaceText(xpub, validXpub)
        tryCompare(form, "canCreate", true)

        tryCompare(form, "canCreate", true)
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
        tryCompare(form.primaryButton, "enabled", true)
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

    function test_wallet_form_tab_order_data() {
        return [
            { tag: "encrypted", encrypted: true },
            { tag: "unencrypted", encrypted: false }
        ]
    }

    function test_wallet_form_tab_order(data) {
        const modal = createTemporaryObject(modalComponent, this)
        modal.open()
        tryCompare(modal, "opened", true)
        findChild(modal, "walletTypeRegular").clicked()
        tryCompare(modal.flow.currentItem, "objectName", "createWalletFormPage")
        tryCompare(modal.flow, "busy", false)
        const form = modal.flow.currentItem
        const name = findChild(form, "createWalletNameInput")
        testSetup.replaceText(name, "Keyboard wallet")
        findChild(form, "createWalletEncryptCheckBox").checked = data.encrypted
        if (data.encrypted) {
            findChild(form, "createWalletPasswordInput").text = "correct horse battery staple"
            findChild(form, "createWalletPasswordRepeatInput").text = "correct horse battery staple"
            findChild(form, "createWalletPasswordConfirmToggle").checked = true
        } else {
            findChild(form, "createWalletUnencryptedConfirmToggle").checked = true
        }
        // Name validation is asynchronous.
        tryVerify(function() { return form.primaryButton.enabled })
        const back = findChild(modal.flow.navigationBar, "onboardingBackButton")
        const order = []
        let item = back
        do {
            order.push(item)
            item = item.nextItemInFocusChain(true)
        } while (item !== back && order.length < 100)
        const expected = [back, findChild(modal.flow.navigationBar, "onboardingCloseButton"),
                          name, findChild(form, "createWalletEncryptCheckBox")]
        if (data.encrypted) {
            expected.push(findChild(form, "createWalletPasswordInput"),
                          findChild(form, "createWalletPasswordRepeatInput"),
                          findChild(form, "createWalletPasswordConfirmToggle"))
        } else {
            expected.push(findChild(form, "createWalletUnencryptedConfirmToggle"))
        }
        expected.push(form.primaryButton)
        let previous = -1
        for (const control of expected) {
            const position = order.indexOf(control)
            verify(position > previous, control.objectName + " must follow the previous control")
            previous = position
        }
        modal.close()
        tryCompare(modal, "visible", false)
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
        compare(acknowledgement.checked, false)
        testSetup.replaceText(findChild(form, "createWalletNameInput"), "Regular wallet")
        password.text = "correct horse battery staple"
        confirm.text = "different"
        compare(form.canCreate, false)
        confirm.text = password.text
        compare(form.canCreate, false)
        acknowledgement.toggle()
        compare(acknowledgement.checked, true)
        tryCompare(form, "canCreate", true)
        encrypt.checked = false
        compare(acknowledgement.checked, false)
        compare(unencryptedAcknowledgement.visible, true)
        compare(unencryptedAcknowledgement.checked, false)
        compare(form.canCreate, false)
        compare(create.enabled, false)
        unencryptedAcknowledgement.toggle()
        compare(unencryptedAcknowledgement.checked, true)
        tryCompare(form, "canCreate", true)
        encrypt.checked = true
        encrypt.checked = false
        compare(unencryptedAcknowledgement.checked, false)
        compare(form.canCreate, false)
        unencryptedAcknowledgement.toggle()
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

    // #1139: Return outside a button must not create a wallet that can never
    // be encrypted.
    function test_return_key_in_the_unencrypted_warning_does_not_create_the_wallet() {
        const flow = createFlow()
        findChild(flow, "walletTypeRegular").clicked()
        tryVerify(function() { return findChild(flow, "createWalletFormPage") !== null })
        const form = findChild(flow, "createWalletFormPage")
        const warning = findChild(form, "createWalletUnencryptedWarning")
        testSetup.replaceText(findChild(form, "createWalletNameInput"), "Regular wallet")
        findChild(form, "createWalletEncryptCheckBox").checked = false
        findChild(form, "createWalletUnencryptedConfirmToggle").toggle()
        tryCompare(form, "canCreate", true)
        findChild(form, "createWalletFormCreateButton").clicked()
        tryCompare(warning, "opened", true)
        keyClick(Qt.Key_Return)
        tryCompare(warning, "visible", false)
        wait(100)
        compare(findChild(flow, "walletCreationReadyPage"), null)
        compare(findChild(form, "createWalletFormCreateButton") !== null, true)
    }

    function test_encrypted_ready_keeps_password_backup_guidance() {
        const flow = createFlow()
        findChild(flow, "walletTypeRegular").clicked()
        tryVerify(function() { return findChild(flow, "createWalletFormPage") !== null })
        const form = findChild(flow, "createWalletFormPage")
        testSetup.replaceText(findChild(form, "createWalletNameInput"), "Encrypted wallet")
        findChild(form, "createWalletPasswordInput").text = "correct horse battery staple"
        findChild(form, "createWalletPasswordRepeatInput").text = "correct horse battery staple"
        compare(form.canCreate, false)
        findChild(form, "createWalletPasswordConfirmToggle").toggle()
        tryCompare(form, "canCreate", true)
        let warningOpened = false
        const warning = findChild(form, "createWalletUnencryptedWarning")
        warning.visibleChanged.connect(function() {
            if (warning.visible) warningOpened = true
        })
        tryCompare(form, "canCreate", true)
        findChild(form, "createWalletFormCreateButton").clicked()
        tryVerify(function() { return findChild(flow, "walletCreationReadyPage") !== null })
        compare(warningOpened, false)
        const ready = findChild(flow, "walletCreationReadyPage")
        compare(ready.encryptedWallet, true)
        verify(findChild(ready, "walletCreationBackupSection").description.indexOf("password") !== -1)
    }

    function test_import_choice_uses_onboarding_error_and_complete_pages() {
        const flow = createFlow()
        walletController.nextRestoreError = "Data is not in recognized format."
        flow.importFile("/tmp/invalid-wallet.bak")
        tryVerify(function() { return findChild(flow, "walletImportErrorPage") !== null })
        const errorPage = findChild(flow, "walletImportErrorPage")
        compare(findChild(errorPage, "importWalletErrorView").visible, true)
        compare(findChild(errorPage, "importWalletErrorTitle").text,
            flow.operation.errorTitle)
        compare(findChild(errorPage, "importWalletErrorDescription").text,
            flow.operation.errorDescription)

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
        compare(infoSection.surfaceLevel, 1)
        compare(findChild(complete, "importWalletSuccessWalletName").text, testWalletModel.displayName)
        compare(findChild(complete, "importWalletSuccessKeyScheme").text, "Single-key")
        compare(complete.primaryButton.visible, false)
        compare(complete.secondaryButton.buttonSize, NeutralButton.Large)
        let finished = false
        flow.finished.connect(function(openActivity) { finished = openActivity })
        findChild(complete, "importWalletSuccessOverviewButton").clicked()
        compare(finished, true)
    }

    function createEncryptedWallet(flow) {
        findChild(flow, "walletTypeRegular").clicked()
        tryVerify(function() { return findChild(flow, "createWalletFormPage") !== null })
        const form = findChild(flow, "createWalletFormPage")
        testSetup.replaceText(findChild(form, "createWalletNameInput"), "Created wallet")
        findChild(form, "createWalletPasswordInput").text = "correct horse battery staple"
        findChild(form, "createWalletPasswordRepeatInput").text = "correct horse battery staple"
        findChild(form, "createWalletPasswordConfirmToggle").toggle()
        tryCompare(form, "canCreate", true)
        findChild(form, "createWalletFormCreateButton").clicked()
        tryVerify(function() { return findChild(flow, "walletCreationReadyPage") !== null })
        return findChild(flow, "walletCreationReadyPage")
    }

    // The completion page acts on the wallet it announces, not on whichever
    // wallet another action selected meanwhile.
    function test_completion_acts_on_the_created_wallet() {
        walletController.setOpenWallets([testWalletModel, testSecondWalletModel])
        walletController.setSelectedWalletObject(testWalletModel)
        walletController.openedWallet = testSecondWalletModel
        try {
            const flow = createFlow()
            const ready = createEncryptedWallet(flow)
            compare(flow.completedWallet, testSecondWalletModel)
            compare(ready.wallet, testSecondWalletModel)

            findChild(ready, "createWalletBackupViewFileButton").clicked()
            compare(walletController.lastLocationWallet, testSecondWalletModel)

            let finishedWallet = null
            flow.finished.connect(function(openActivity, wallet) { if (openActivity) finishedWallet = wallet })
            findChild(ready, "createWalletReadyDoneButton").clicked()
            compare(finishedWallet, testSecondWalletModel)
        } finally {
            walletController.setOpenWallets([testWalletModel])
        }
    }

    function test_completion_closes_when_its_wallet_closes() {
        walletController.setOpenWallets([testWalletModel, testSecondWalletModel])
        walletController.openedWallet = testSecondWalletModel
        try {
            let flow = createFlow()
            createEncryptedWallet(flow)
            let closed = false
            flow.finished.connect(function(openActivity, wallet) { closed = !openActivity && wallet === null })
            walletController.setOpenWallets([testWalletModel])
            verify(closed)

            walletController.setOpenWallets([testWalletModel, testSecondWalletModel])
            flow = createFlow()
            flow.importFile("/tmp/imported.bak")
            tryVerify(function() { return findChild(flow, "importWalletSuccessPage") !== null })
            closed = false
            flow.finished.connect(function(openActivity, wallet) { closed = !openActivity && wallet === null })
            testSecondWalletModel.available = false
            verify(closed)
        } finally {
            testSecondWalletModel.available = true
            walletController.setOpenWallets([testWalletModel])
        }
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
    function test_restore_keeps_original_source_while_held_data() {
        return [{tag: "spaces", path: "/tmp/Savings .bak"}, {tag: "literal-percent", path: "/tmp/Savings%20.legacy.bak"}, {tag: "unicode", path: "/tmp/Trésor #1.bak"}]
    }
    function test_restore_keeps_original_source_while_held(data) {
        walletController.holdRestore = true
        const flow = createFlow()
        flow.importFile(data.path)
        const original = flow.operation
        verify(original !== null)
        compare(original.kind, WalletOperationModel.Restore)
        verify(flow.importingWallet)
        compare(walletController.lastRestoreSource, data.path)
        flow.importFile("/tmp/replacement.bak")
        compare(walletController.restoreCalls, 1)
        compare(flow.operation, original)
        walletController.setSelectedWalletObject(testSecondWalletModel)
        walletController.finishRestore()
        tryCompare(original, "phase", WalletOperationModel.Succeeded)
        compare(flow.operation, original)
        compare(walletController.lastRestoreSource, data.path)
        compare(walletController.selectedWallet, testSecondWalletModel)
    }
    function test_restore_failure_and_warnings_belong_to_original_operation() {
        walletController.holdRestore = true
        walletController.nextRestoreWarnings = "Original fallback fee warning"
        const flow = createFlow()
        flow.importFile("/tmp/audit_migration_73.bak")
        const original = flow.operation
        walletController.finishRestore("A wallet database already exists at the destination")
        tryCompare(flow.currentItem, "objectName", "walletImportErrorPage")
        const title = findChild(flow.currentItem, "importWalletErrorTitle").text
        walletController.walletLoadError = "Unrelated legacy wallet"
        walletController.walletLoadErrorChanged()
        compare(flow.operation, original)
        compare(findChild(flow.currentItem, "importWalletErrorTitle").text, title)
        compare(original.error.message, "A wallet database already exists at the destination")
        compare(original.result.warnings[0], "Original fallback fee warning")
    }
    function test_detached_restore_does_not_select_its_successor() {
        walletController.holdRestore = true
        const flow = flowComponent.createObject(this)
        verify(flow !== null)
        flow.importFile("/tmp/original.bak")
        const original = flow.operation
        flow.destroy()
        walletController.setSelectedWalletObject(testSecondWalletModel)
        walletController.finishRestore()
        tryCompare(original, "phase", WalletOperationModel.Succeeded)
        compare(walletController.selectedWallet, testSecondWalletModel)
        compare(walletController.restoreCalls, 1)
    }
    function test_partial_restore_never_restarts_the_copy() {
        walletController.holdRestore = true
        const flow = createFlow()
        flow.importFile("/tmp/original.bak")
        const original = flow.operation
        walletController.finishRestore("Prune: required blocks unavailable", true)
        tryCompare(flow.currentItem, "objectName", "walletCreationOutcomePage")
        compare(original.phase, WalletOperationModel.Partial)
        verify(findChild(flow.currentItem, "walletCreationRecoveryPaths").text.length > 0)
        findChild(flow.currentItem, "walletCreationOutcomeDoneButton").clicked()
        compare(walletController.restoreCalls, 1)
        compare(flow.operation, original)
    }

}

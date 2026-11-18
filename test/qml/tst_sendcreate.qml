// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.2
import org.bitcoincore.qt 1.0
import "../../qml/controls"
import "../../qml/pages/wallet"
import "../../qml/components"

TestCase {
    id: testCase
    name: "SendCreate"
    when: windowShown
    width: 900
    height: 700

    property string motionMode: ""

    Component {
        id: sendComponent

        SendCreate { wallet: testWalletModel }
    }
    Component { id: desktopMailboxComponent; DesktopIntentModel {} }

    Component {
        id: reviewComponent

        SendTransactionReview { wallet: testWalletModel }
    }

    function confirmExplicitImport(form) {
        const operation = form.imports.operation
        if (operation && operation.promptToken) {
            compare(operation.state, PaymentImportOperation.AwaitingReplacement)
            form.imports.replace()
        }
    }

    function cleanup() {
        Theme.motion.mode = motionMode
        // Persistent model signals can outlive QtTest's deferred temporary
        // destruction. Detach while each SignalSpy's QML context is alive.
        for (let i = 0; i < sendGapSpies.length; ++i) sendGapSpies[i].target = null
        sendGapSpies = []
    }

    function init() {
        motionMode = Theme.motion.mode
        testWalletTransaction.setNodePolicyStatusForTest("")
        testSetup.failOnWarnings()
        BitcoinUri.releasePublications()
        BitcoinUri.resetRequestControlsForTest()
        walletController.setInitialized(true)
        walletController.setSelectedWallet("send-create-test-wallet")
        testWalletModel.sendDraftSweepsWallet = false
        testWalletModel.currentTransactionSweepsWallet = false
        testWalletModel.useMaximumCalls = 0
        testWalletModel.clearMaximum()
        testWalletModel.resetSendForTest()
        testWalletModel.sendTransactionResult = true
        testWalletModel.transactionNeedsUnlock = false
        testWalletModel.feeEstimatePending = false
        testWalletModel.resetFeePolicyForTest()
        tryCompare(testWalletModel.fees, "policyPending", false)
        testWalletModel.targetBlocks = 6
        testWalletModel.prepareTransactionResult = true
        testWalletModel.sendAmountExhaustsBalance = false
        testWalletModel.currentTransactionCanSend = true
        testWalletModel.currentTransactionCanSignOnly = false
        testWalletModel.currentTransactionNeedsRevalidation = false
        testWalletModel.transactionFeePending = false
        testWalletModel.currentTransactionCanBroadcast = false
        testWalletModel.currentTransactionIsImportedPsbt = false
        testWalletModel.currentTransactionReviewMessage = ""
        testSendRecipient.address.setAddress("bcrt1qsendtoaddress")
        testSendRecipient.addressError = ""
        testSendRecipient.amount.display = "0.00000000"
        testSendRecipient.amountError = ""
        testSendRecipient.label = ""
        testSendRecipient.hasPaymentRequest = false
        testSendRecipient.paymentRequestLabel = ""
        testSendRecipient.message = ""
        testSendRecipient.isValid = true
        testRecipientsModel.allValid = true
        testRecipientsModel.validationError = ""
        testRecipientsModel.clearToFront()
        testRecipientsModel.totalAmountSatoshi = 0
        testWalletTransaction.reviewedRecipients = [{
            address: "bcrt1qsendtoaddress", formattedAddress: "bcrt1qsendtoaddress",
            label: "recipient-1", amount: "0.00100000 BTC", hasPaymentRequest: false
        }]
        testWalletTransaction.reviewTargetBlocks = 6
        testWalletTransaction.reviewFeeRate = "2.5"
        testWalletTransaction.reviewFeeSource = FeeSelectionModel.Estimated
        testWalletTransaction.reviewUsesFallback = false
        optionsModel.displayUnit = BitcoinAmount.BTC
        testCoinsListModel.reset()
        Clipboard.currentText = ""
    }

    function pasteShortcut() {
        keyClick(Qt.Key_V, Qt.ControlModifier)
    }

    function test_custom_entry_waits_only_for_a_derived_default() {
        const page = createTemporaryObject(sendComponent, testCase.Window.window.contentItem, {width: 1000, height: 900})
        const field = findChild(page, "feeSelectionCustomRateInput")
        testWalletModel.fees.chooseCustomTarget(50)
        compare(testWalletModel.fees.customDefaultPending, true)
        compare(field.enabled, false)
        tryCompare(testWalletModel.fees, "customDefaultPending", false)
        compare(field.enabled, true)
        testWalletModel.fees.customRate.edit("17")
        compare(testWalletModel.fees.policyPending, true)
        compare(testWalletModel.fees.customDefaultPending, false)
        compare(field.enabled, true)
        tryCompare(testWalletModel.fees.customRate, "valid", true)
        compare(field.text, "17")
    }
    function test_checked_custom_reselection_preserves_explicit_rate() {
        const page = createTemporaryObject(sendComponent, testCase.Window.window.contentItem, {width: 1000, height: 900})
        const picker = findChild(page, "feeSelectionPicker")
        picker.open(); picker.itemAtIndex(3).clicked()
        tryCompare(testWalletModel.fees, "policyPending", false)
        testWalletModel.fees.customRate.edit("17")
        tryCompare(testWalletModel.fees.customRate, "valid", true)
        picker.open(); picker.itemAtIndex(3).clicked()
        compare(testWalletModel.fees.customRate.text, "17")
        compare(findChild(page, "feeSelectionCustomRateInput").text, "17")
        compare(testWalletModel.fees.policyPending, false)
    }
    function test_oversized_fee_paste_does_not_accept_a_valid_prefix() {
        const page = createTemporaryObject(sendComponent, testCase.Window.window.contentItem, {width: 1000, height: 900})
        testWalletModel.fees.chooseCustom()
        tryCompare(testWalletModel.fees, "policyPending", false)
        const field = findChild(page, "feeSelectionCustomRateInput")
        const previous = testSetup.nativeClipboardText()
        try {
            testSetup.setNativeClipboardText("1" + " ".repeat(40))
            field.forceActiveFocus(); keyClick(Qt.Key_A, Qt.ControlModifier)
            keyClick(Qt.Key_V, Qt.ControlModifier)
            tryCompare(testWalletModel.fees, "policyPending", false)
            compare(field.maximumLength, 33)
            compare(testWalletModel.fees.customRate.text.length, 33)
            compare(testWalletModel.fees.customRate.valid, false)
            compare(findChild(page, "sendReviewButton").enabled, false)
        } finally { testSetup.setNativeClipboardText(previous) }
    }
    function test_equal_fee_rates_preserve_actual_provenance_data() {
        return [{tag: "fallback", source: FeeSelectionModel.Fallback, warning: true},
                {tag: "estimator", source: FeeSelectionModel.Estimated, warning: false}]
    }
    function test_equal_fee_rates_preserve_actual_provenance(data) {
        const page = createTemporaryObject(sendComponent, testCase.Window.window.contentItem, {width: 1000, height: 900})
        testWalletModel.publishFeeResultForTest(data.source, "")
        compare(findChild(page, "feeEstimateProvenanceText").visible, data.warning)
        compare(testWalletModel.fees.hasEstimatorTarget, !data.warning)
        compare(testWalletModel.fees.estimatedRate, "2.500")
    }
    function test_failed_maximum_exposes_original_cause_and_refresh() {
        const page = createTemporaryObject(sendComponent, testCase.Window.window.contentItem, {width: 1000, height: 900})
        findChild(page, "sendUseMaximumButton").clicked()
        testWalletModel.sendAmountExhaustsBalance = true
        testWalletModel.publishFeeResultForTest(FeeSelectionModel.Unknown, "Transaction too large.")
        compare(page.formErrorText, "Transaction too large.")
        compare(findChild(page, "feeEstimateErrorText").text, "Transaction too large.")
        compare(findChild(page, "feeRefreshButton").visible, true)
        compare(findChild(page, "sendAmountStatusText").text, "Previous amount; maximum could not be calculated.")
        compare(findChild(page, "sendReviewButton").enabled, false)
    }
    function test_review_provenance_is_immutable_presentation() {
        testWalletTransaction.reviewFeeSource = FeeSelectionModel.Fallback
        testWalletTransaction.reviewUsesFallback = true
        const page = createTemporaryObject(reviewComponent, testCase.Window.window.contentItem, {width: 1000, height: 900})
        compare(findChild(page, "sendReviewFallbackFeeWarning").visible, true)
        compare(findChild(page, "sendTransactionReviewTargetBlocks").visible, false)
        testWalletModel.fees.choosePreset(2)
        tryCompare(testWalletModel.fees, "policyPending", false)
        compare(findChild(page, "sendReviewFallbackFeeWarning").visible, true)
        compare(testWalletTransaction.reviewFeeRate, "2.5")
    }

    function test_send_button_requires_the_current_draft_review() {
        const review = createTemporaryObject(reviewComponent, testCase.Window.window.contentItem, {width: 1000, height: 900})
        const sendButton = findChild(review, "sendTransactionReviewSendButton")
        verify(sendButton !== null)
        compare(sendButton.enabled, true)

        testWalletModel.setCurrentTransactionNeedsRevalidationForTest(true)
        tryCompare(sendButton, "visible", false)
        tryCompare(sendButton, "enabled", false)
        const warning = findChild(review, "sendTransactionReviewWarning")
        verify(warning !== null)
        tryCompare(warning, "visible", true)
        compare(warning.message, "The draft changed after review. Prepare it again before sending.")
        compare(testWalletModel.sendTransactionCalls, 0)

        testWalletModel.setCurrentTransactionNeedsRevalidationForTest(false)
        tryCompare(sendButton, "visible", true)
        tryCompare(sendButton, "enabled", true)
    }

    function test_imported_review_explains_policy_and_rechecks_without_sending() {
        testWalletModel.currentTransactionIsImportedPsbt = true
        testWalletModel.currentTransactionCanSend = false
        testWalletModel.currentTransactionCanBroadcast = false
        testWalletTransaction.setNodePolicyStatusForTest("An input is already spent. Recheck before submitting.")
        const calls = testWalletModel.recheckTransactionCalls()
        const review = createTemporaryObject(reviewComponent, testCase.Window.window.contentItem, {width: 1000, height: 900})
        const explanation = findChild(review, "sendReviewNodePolicy")
        const recheck = findChild(review, "sendReviewRecheck")
        verify(explanation.visible)
        compare(explanation.text, "An input is already spent. Recheck before submitting.")
        verify(recheck.visible)
        verify(recheck.enabled)
        recheck.clicked()
        compare(testWalletModel.recheckTransactionCalls(), calls + 1)
        compare(testWalletModel.sendTransactionCalls, 0)
        compare(testWalletModel.broadcastCurrentTransactionCalls, 0)
    }

    function test_local_sign_only_is_a_separate_non_send_command() {
        testWalletModel.currentTransactionCanSignOnly = true
        const review = createTemporaryObject(reviewComponent, testCase.Window.window.contentItem, {width: 1000, height: 900})
        const signButton = findChild(review, "sendTransactionReviewSignOnlyButton")
        verify(signButton !== null)
        compare(signButton.visible, true)
        compare(signButton.enabled, true)
        compare(testWalletModel.sendTransactionCalls, 0)

        signButton.clicked()

        compare(testWalletModel.signOnlyTransactionCalls, 1)
        compare(testWalletModel.sendTransactionCalls, 0)
        compare(testWalletModel.sendState, WalletQmlModel.SendSignedOnly)
    }

    function test_locked_sign_only_prompt_says_transaction_will_not_be_sent() {
        testWalletModel.currentTransactionCanSignOnly = true
        testWalletModel.transactionNeedsUnlock = true
        const review = createTemporaryObject(reviewComponent, testCase.Window.window.contentItem, {width: 1000, height: 900})
        findChild(review, "sendTransactionReviewSignOnlyButton").clicked()

        const popup = findChild(review, "sendTransactionPassphrasePopup")
        tryCompare(popup, "opened", true)
        compare(popup.descriptionText, "Enter your wallet password to sign this PSBT. The transaction will not be sent.")
        compare(popup.confirmText, "Unlock and sign")
        compare(testWalletModel.sendTransactionCalls, 0)
    }

    function test_equal_amount_intent_uses_the_typed_fixed_mode() {
        const page = createTemporaryObject(sendComponent, testCase.Window.window.contentItem, {width: 1000, height: 900})
        testSendRecipient.amount.satoshi = 12345
        findChild(page, "sendUseMaximumButton").clicked()
        compare(testWalletModel.useMaximumCalls, 1)
        compare(testSendRecipient.amountMode, SendRecipient.Maximum)
        testSendRecipient.amount.editInput(testSendRecipient.amount.inputText)
        compare(testSendRecipient.amountMode, SendRecipient.Fixed)
        compare(testWalletModel.maximumRecipient, null)
        compare(testSendRecipient.amount.satoshi, 12345)
    }
    function test_draft_revision_binds_to_the_owner_across_view_recreation() {
        const page = createTemporaryObject(sendComponent, testCase.Window.window.contentItem, {width: 1000, height: 900})
        compare(typeof page.draftRevision, "string")
        compare(page.draftRevision, testRecipientsModel.revision)
        const before = page.draftRevision
        testSendRecipient.amount.editInput(testSendRecipient.amount.inputText)
        verify(page.draftRevision !== before)
        compare(page.draftRevision, testRecipientsModel.revision)
        const recreated = createTemporaryObject(sendComponent, testCase.Window.window.contentItem, {width: 1000, height: 900})
        compare(recreated.draftRevision, page.draftRevision)
    }
    function test_recipient_budget_binds_the_add_command() {
        const page = createTemporaryObject(sendComponent, testCase.Window.window.contentItem, {width: 1000, height: 900})
        for (let row = 1; row < 25; ++row) testRecipientsModel.add()
        compare(testRecipientsModel.count, 25)
        compare(testRecipientsModel.canAddRecipient, false)
        compare(findChild(page, "sendAddRecipientButton").enabled, false)
    }
    function test_private_note_transport_refusal_restores_the_owned_text() {
        const page = createTemporaryObject(sendComponent, testCase.Window.window.contentItem, {width: 1000, height: 900})
        testSendRecipient.editPrivateNote("owned private note")
        const note = findChild(page, "sendNoteInput")
        verify(note !== null)
        note.forceActiveFocus()
        note.text = "x".repeat(257)
        tryCompare(note, "text", "owned private note")
        compare(testSendRecipient.label, "owned private note")
    }

    function test_available_balance_keeps_caption_static() {
        Theme.motion.mode = "full"
        const page = createTemporaryObject(sendComponent, testCase.Window.window.contentItem)
        verify(page !== null)
        page.width = testCase.width
        page.height = testCase.height
        page.visible = true

        const caption = findChild(page, "sendAvailableCaption")
        const amount = findChild(page, "sendAvailableAmount")
        verify(caption !== null)
        verify(amount !== null)
        compare(caption.text, "Available")
        verify(amount.text.length > 0)
        optionsModel.displayUnit = BitcoinAmount.SAT
        tryCompare(amount, "animating", true)
        tryCompare(amount, "animating", false)
        optionsModel.displayUnit = BitcoinAmount.BTC
        tryCompare(amount, "animating", true)
        tryCompare(amount, "animating", false)
        verify(amount.text.endsWith(" BTC"))
        compare(caption.text, "Available")
    }

    function test_review_only_psbt_closes_and_discards_on_wallet_change() {
        const page = createTemporaryObject(sendComponent, testCase.Window.window.contentItem)
        verify(page !== null)
        page.width = testCase.width
        page.height = testCase.height
        page.visible = true

        const popup = findChild(page, "transactionReviewPopup")
        verify(popup !== null)

        testWalletModel.currentTransactionCanSend = false
        testWalletModel.currentTransactionReviewMessage = "This wallet cannot sign."
        const discardCallsBefore = testWalletModel.discardCurrentTransactionCalls

        popup.reviewWallet = testWalletModel
        popup.importedReview = true
        popup.open()
        tryCompare(popup, "opened", true)

        page.wallet = testSecondWalletModel

        tryCompare(popup, "opened", false)
        tryCompare(testWalletModel, "discardCurrentTransactionCalls", discardCallsBefore + 1)
    }

    function test_pending_psbt_import_keeps_its_intent_data() {
        return [
            {tag: "coin-refresh", change: "refresh", opens: true},
            {tag: "coin-selection", change: "selection", opens: false},
            {tag: "coin-selection-pending", change: "selection-pending", opens: false},
            {tag: "recipient-edit", change: "recipient", opens: false},
            {tag: "hidden-page", change: "hidden", opens: false}
        ]
    }

    function test_pending_psbt_import_keeps_its_intent(data) {
        const page = createTemporaryObject(sendComponent, testCase.Window.window.contentItem,
                {width: testCase.width, height: testCase.height, visible: true})
        verify(page !== null)
        const popup = findChild(page, "transactionReviewPopup")
        testWalletModel.holdPsbtPublicationForTest = true
        page.requestPsbtImport("original-review.psbt")
        verify(page.psbtImportTicket > 0)
        testWalletModel.completePsbtWorkForTest()
        compare(testWalletModel.psbtActionBusy, true)

        // Hold the completed import while a separate GUI observation arrives.
        if (data.change === "refresh") testCoinsListModel.selectedCoinsCountChanged()
        else if (data.change === "selection") testCoinsListModel.toggleCoinSelection(0)
        else if (data.change === "selection-pending") testCoinsListModel.selectionPending = true
        else if (data.change === "recipient") testSendRecipient.label = "changed intent"
        else page.visible = false

        testWalletModel.finishPsbtPublicationForTest()
        compare(testWalletModel.psbtActionState, WalletQmlModel.PsbtImported)
        compare(page.psbtImportTicket, 0)
        if (data.opens) tryCompare(popup, "opened", true)
        else compare(popup.visible, false)
        popup.close()
        tryCompare(popup, "visible", false)
    }

    function test_successful_psbt_broadcast_pushes_send_complete() {
        const page = createTemporaryObject(sendComponent, testCase.Window.window.contentItem)
        verify(page !== null)
        page.width = testCase.width
        page.height = testCase.height
        page.visible = true

        const reviewPopup = findChild(page, "transactionReviewPopup")
        const reviewStack = findChild(reviewPopup, "sendTransactionReviewStack")
        verify(reviewPopup !== null)
        verify(reviewStack !== null)

        testWalletModel.currentTransactionCanSend = false
        testWalletModel.currentTransactionCanBroadcast = true
        testWalletModel.currentTransactionIsImportedPsbt = true
        reviewPopup.reviewWallet = testWalletModel
        reviewPopup.importedReview = true
        reviewPopup.open()
        tryCompare(reviewPopup, "opened", true)

        const broadcastButton = findChild(reviewPopup, "sendTransactionReviewSendButton")
        verify(broadcastButton !== null)
        tryCompare(broadcastButton, "visible", true)
        const broadcastCallsBefore = testWalletModel.broadcastCurrentTransactionCalls
        const discardCallsBefore = testWalletModel.discardCurrentTransactionCalls

        broadcastButton.clicked()

        tryCompare(reviewStack, "depth", 2)
        compare(reviewPopup.opened, true)
        const completePage = findChild(reviewStack, "sendCompletePage")
        verify(completePage !== null)
        compare(completePage.targetBlocks, 0)
        compare(completePage.submissionState, WalletQmlModel.SubmissionAccepted)
        compare(completePage.recordState, WalletQmlModel.SendRecordUnknown)
        verify(completePage.descriptionText.indexOf("fee rate") >= 0)
        tryCompare(testWalletModel, "broadcastCurrentTransactionCalls", broadcastCallsBefore + 1)
        findChild(reviewStack, "sendResultDoneButton").clicked()
        tryCompare(reviewPopup, "opened", false)
        tryCompare(testWalletModel, "discardCurrentTransactionCalls", discardCallsBefore + 1)
    }

    function test_review_recipient_card_handles_one_and_multiple_recipients() {
        const page = createTemporaryObject(sendComponent, testCase.Window.window.contentItem)
        verify(page !== null)
        page.width = testCase.width
        page.height = testCase.height
        page.visible = true
        const popup = findChild(page, "transactionReviewPopup")
        verify(popup !== null)
        page.openTransactionReview()
        tryCompare(popup, "opened", true)

        const reviewTitle = findChild(popup.contentItem, "sendTransactionReviewTitle")
        const closeButton = findChild(popup.contentItem, "sendTransactionReviewCloseButton")
        verify(reviewTitle !== null)
        verify(closeButton !== null)
        compare(reviewTitle.text, "Review transaction")
        compare(closeButton.size, Theme.size.iconButton)
        compare(closeButton.iconSize, Theme.size.icon.s)
        compare(closeButton.background.radius, closeButton.size / 2)
        const summary = findChild(popup.contentItem, "sendTransactionReviewSummary")
        compare(summary.amount, testWalletTransaction.total)
        const icon = findChild(summary, "sendTransactionReviewIcon")
        verify(icon.pending)
        verify(icon.dashed)
        const amountText = findChild(summary, "transactionSummaryAmount")
        const amountUnit = findChild(summary, "transactionSummaryUnit")
        tryVerify(function() { return amountText.width > 0 && amountUnit.width > 0 })
        const amountLeft = amountText.mapToItem(summary, 0, 0).x
        const unitRight = amountUnit.mapToItem(summary, amountUnit.width, 0).x
        verify(Math.abs((amountLeft + unitRight) / 2 - summary.width / 2) < 2)
        compare(findChild(popup.contentItem, "sendTransactionReviewReadyPill").text,
                "Ready to send")
        const saveButton = findChild(popup.contentItem, "sendTransactionReviewSaveButton")
        const sendButton = findChild(popup.contentItem, "sendTransactionReviewSendButton")
        tryVerify(function() { return saveButton.x < sendButton.x })
        compare(saveButton.y, sendButton.y)

        const first = findChild(popup.contentItem, "sendTransactionReviewRecipient0")
        verify(first !== null)
        const feeSection = findChild(popup.contentItem, "sendTransactionReviewNetworkFee")
        verify(feeSection !== null)
        compare(feeSection.visible, true)
        compare(findChild(feeSection, "sendTransactionReviewTargetBlocks").value, "6 blocks")
        compare(findChild(feeSection, "sendTransactionReviewFeeRate").value, "2.5 sat/vB")
        testWalletModel.targetBlocks = 10
        testWalletModel.customFeeEnabled = true
        testWalletModel.customFeeRate = "4.2"
        compare(findChild(feeSection, "sendTransactionReviewTargetBlocks").value, "6 blocks")
        compare(findChild(feeSection, "sendTransactionReviewFeeRate").value, "2.5 sat/vB")
        compare(findChild(first, "sendTransactionReviewRecipient0Title").text, "recipient-1")
        compare(findChild(first, "sendTransactionReviewRecipient0Description").text, "bcrt1qsendtoaddress")
        testSendRecipient.amount.display = "1234.00000000"
        compare(findChild(first, "sendTransactionReviewRecipient0Amount").text, "0.00100000 BTC")
        const requestBadge = findChild(first, "sendTransactionReviewRecipient0RequestBadge")
        verify(requestBadge !== null)
        compare(requestBadge.visible, false)
        const recipientTitle = findChild(first, "sendTransactionReviewRecipient0Title")
        const titleX = recipientTitle.mapToItem(first, 0, 0).x
        testSendRecipient.hasPaymentRequest = true
        compare(requestBadge.visible, false)
        compare(recipientTitle.mapToItem(first, 0, 0).x, titleX)
        compare(requestBadge.inactive, true)
        compare(requestBadge.statusText, "Payment request recipient")
        testSendRecipient.hasPaymentRequest = false

        testRecipientsModel.add()
        compare(findChild(popup.contentItem, "sendTransactionReviewRecipient1"), null)
        testWalletTransaction.reviewedRecipients = testWalletTransaction.reviewedRecipients.concat([{
            address: "bcrt1qsecondaddress", formattedAddress: "bcrt1qsecondaddress",
            label: "recipient-2", amount: "0.00200000 BTC", hasPaymentRequest: true
        }])
        tryVerify(function() { return findChild(popup.contentItem, "sendTransactionReviewRecipient1") !== null })
        const second = findChild(popup.contentItem, "sendTransactionReviewRecipient1")
        verify(second !== null)
        compare(findChild(second, "sendTransactionReviewRecipient1Title").text, "recipient-2")
        compare(findChild(popup.contentItem, "sendTransactionReviewRecipient0").showDivider, true)
        compare(second.showDivider, false)
        compare(findChild(second, "sendTransactionReviewRecipient1RequestBadge").visible, true)
        popup.close()
    }

    function test_large_review_recipients_virtualize_and_preserve_complete_late_entry() {
        const literal = "private-note-".repeat(8192)
        const recipients = []
        for (let i = 0; i < 1001; ++i) recipients.push({address: "address-" + i, formattedAddress: "address-" + i,
            label: i === 1000 ? literal : "recipient-" + i, amountSat: 1000, amount: "0.00001000 BTC", hasPaymentRequest: false})
        testWalletTransaction.reviewedRecipients = recipients
        const review = createTemporaryObject(reviewComponent, testCase.Window.window.contentItem,
            {width: 1000, height: 800, visible: true})
        const rows = findChild(review, "sendTransactionReviewRecipientRows")
        tryCompare(rows, "count", 1001)
        verify(waitForRendering(review))
        verify(testSetup.namedChildCount(review, "sendReviewRecipientPreview") < 32)
        review.openReviewEntry("recipients", 1000)
        const popup = findChild(review, "sendReviewEntryPopup")
        tryCompare(popup, "opened", true)
        tryVerify(function() { return !testWalletModel.currentTransactionFlowDetails.entryDetailsBusy })
        compare(testSetup.entryFieldText(testWalletModel.currentTransactionFlowDetails.entryDocument, "label"), literal)
        review.visible = false
        tryCompare(popup, "visible", false)
    }

    function test_review_content_width_matches_activity_detail() {
        const review = createTemporaryObject(reviewComponent, testCase.Window.window.contentItem)
        verify(review !== null)
        review.width = 1400
        review.height = 800
        review.visible = true
        const recipients = findChild(review, "sendTransactionReviewRecipients")
        tryCompare(recipients, "width", 1100)
        compare(review.header.leftPadding, 150)

        review.width = 390
        tryCompare(recipients, "width", 358)
        compare(review.header.leftPadding, 16)
    }

    function test_pending_psbt_fee_uses_bound_capabilities_data() {
        return [{tag: "trusted", known: true}, {tag: "unavailable", known: false}]
    }

    function test_pending_psbt_fee_uses_bound_capabilities(data) {
        const page = createTemporaryObject(sendComponent, testCase.Window.window.contentItem)
        verify(page !== null)
        page.width = testCase.width
        page.height = testCase.height
        page.visible = true
        testWalletModel.currentTransactionIsImportedPsbt = true
        testWalletModel.currentTransactionCanSend = false
        testWalletModel.currentTransactionCanBroadcast = false
        testWalletModel.transactionFeePending = true
        testWalletModel.currentTransactionReviewMessage = "Checking transaction fee."
        findChild(page, "walletSendPage").handlePsbtImportResult(WalletQmlModel.ReviewPending)
        const popup = findChild(page, "transactionReviewPopup")
        tryCompare(popup, "opened", true)
        const review = findChild(popup, "sendTransactionReviewPage")
        compare(review.feePending, true)
        compare(findChild(popup, "sendTransactionReviewSendButton").visible, false)
        compare(findChild(popup, "sendTransactionReviewSaveButton").visible, true)
        testWalletModel.transactionFeePending = false
        testWalletModel.currentTransactionCanBroadcast = data.known
        testWalletModel.currentTransactionReviewMessage = data.known ? "" : "The transaction fee is missing or invalid."
        tryCompare(review, "feePending", false)
        tryCompare(findChild(popup, "sendTransactionReviewSendButton"), "visible", data.known)
        compare(review.canSend, false)
        popup.close()
    }

    function test_imported_psbt_without_signing_keys_only_offers_save() {
        const page = createTemporaryObject(sendComponent, testCase.Window.window.contentItem)
        verify(page !== null)
        page.width = testCase.width
        page.height = testCase.height
        page.visible = true
        testWalletModel.currentTransactionIsImportedPsbt = true
        testWalletModel.currentTransactionCanSend = false
        testWalletModel.currentTransactionCanBroadcast = false
        testWalletModel.currentTransactionReviewMessage = "This wallet cannot sign."
        page.openTransactionReview()
        const popup = findChild(page, "transactionReviewPopup")
        tryCompare(popup, "opened", true)
        compare(findChild(popup, "sendTransactionReviewSendButton").visible, false)
        compare(findChild(popup, "sendTransactionReviewSaveButton").visible, true)
        compare(findChild(popup, "sendTransactionReviewWarning").visible, true)
        compare(findChild(popup, "sendTransactionReviewNetworkFee").visible, false)
        verify(findChild(popup.contentItem, "sendTransactionReviewReadyPill") === null)
        popup.close()
    }

    function test_completion_retains_the_original_submission_outcome() {
        const page = createTemporaryObject(sendComponent, testCase.Window.window.contentItem)
        verify(page !== null); page.width = testCase.width; page.height = testCase.height; page.visible = true
        testWalletModel.sendSubmission = WalletQmlModel.SubmissionNotInMempool
        page.openTransactionReview()
        const popup = findChild(page, "transactionReviewPopup"), stack = findChild(popup, "sendTransactionReviewStack")
        tryCompare(popup, "opened", true)
        findChild(popup.contentItem, "sendTransactionReviewSendButton").clicked()
        tryCompare(stack, "depth", 2)
        const complete = findChild(stack, "sendCompletePage")
        compare(complete.submissionState, WalletQmlModel.SubmissionNotInMempool)
        compare(complete.titleText, "Transaction saved")
        verify(complete.descriptionText.indexOf("not in the mempool") >= 0)
        testWalletModel.sendSubmission = WalletQmlModel.SubmissionMempool
        compare(complete.submissionState, WalletQmlModel.SubmissionNotInMempool)
        compare(complete.titleText, "Transaction saved")
        verify(complete.descriptionText.indexOf("confirm within") === -1)
    }

    function test_old_send_completion_cannot_complete_a_replacement_review() {
        testWalletModel.holdSendForTest = true
        const review = createTemporaryObject(reviewComponent, testCase.Window.window.contentItem, {width: 1000, height: 900})
        const original = testWalletModel.currentTransaction
        let sent = 0
        review.transactionSent.connect(function() { ++sent })
        try {
            review.commitSend()
            compare(testWalletModel.sendBusy, true)
            testWalletModel.replaceCurrentTransactionForTest(testSecondWalletModel.currentTransaction)
            testWalletModel.finishSendForTest()
            compare(sent, 0)
            compare(review.sending, false)
            compare(testWalletModel.sendState, WalletQmlModel.SendClosed)
        } finally {
            testWalletModel.replaceCurrentTransactionForTest(original)
            testWalletModel.resetSendForTest()
        }
    }

    function test_send_admission_waits_for_the_committed_outcome() {
        testWalletModel.holdSendForTest = true
        const page = createTemporaryObject(sendComponent, testCase.Window.window.contentItem)
        verify(page !== null)
        page.width = testCase.width; page.height = testCase.height; page.visible = true
        page.openTransactionReview()
        const popup = findChild(page, "transactionReviewPopup")
        const stack = findChild(popup, "sendTransactionReviewStack")
        tryCompare(popup, "opened", true)
        const button = findChild(popup.contentItem, "sendTransactionReviewSendButton")
        button.clicked()
        compare(testWalletModel.sendBusy, true)
        compare(stack.depth, 1)
        compare(button.enabled, false)
        const before = testWalletModel.sendTransactionCalls
        button.clicked()
        compare(testWalletModel.sendTransactionCalls, before)
        testWalletModel.finishSendForTest()
        tryCompare(stack, "depth", 2)
        page.destroy()
    }

    function test_review_send_pushes_complete_within_modal() {
        const page = createTemporaryObject(sendComponent, testCase.Window.window.contentItem)
        verify(page !== null)
        page.width = testCase.width
        page.height = testCase.height
        page.visible = true
        page.openTransactionReview()
        const popup = findChild(page, "transactionReviewPopup")
        const reviewStack = findChild(popup, "sendTransactionReviewStack")
        tryCompare(popup, "opened", true)
        compare(reviewStack.depth, 1)
        const sendButton = findChild(popup.contentItem, "sendTransactionReviewSendButton")
        verify(sendButton !== null)
        compare(sendButton.visible, true)
        compare(sendButton.text, "Send transaction")
        const callsBefore = testWalletModel.sendTransactionCalls
        sendButton.clicked()
        tryCompare(reviewStack, "depth", 2)
        compare(popup.opened, true)
        const completePage = findChild(reviewStack, "sendCompletePage")
        verify(completePage !== null)
        compare(completePage.txid, testWalletTransaction.txid)
        compare(completePage.targetBlocks, 6)
        verify(completePage.descriptionText.indexOf("6 blocks") >= 0)
        compare(testWalletModel.sendTransactionCalls, callsBefore + 1)
        testCoinsListModel.setManualInputSelection(true)
        testCoinsListModel.toggleCoinSelection(0)
        findChild(reviewStack, "sendResultDoneButton").clicked()
        tryCompare(popup, "opened", false)
        compare(page.manualCoinSelection, false)
        compare(testCoinsListModel.selectedCoinsCount, 0)
    }

    function test_closing_review_clears_form_only_after_send() {
        const page = createTemporaryObject(sendComponent, testCase.Window.window.contentItem)
        verify(page !== null)
        page.width = testCase.width
        page.height = testCase.height
        page.visible = true
        testCoinsListModel.setManualInputSelection(true)
        page.openTransactionReview()
        const popup = findChild(page, "transactionReviewPopup")
        const reviewStack = findChild(popup, "sendTransactionReviewStack")
        tryCompare(popup, "opened", true)
        popup.close()
        tryCompare(popup, "visible", false)
        compare(page.manualCoinSelection, true)

        page.openTransactionReview()
        tryCompare(popup, "opened", true)
        findChild(popup.contentItem, "sendTransactionReviewSendButton").clicked()
        tryCompare(reviewStack, "depth", 2)
        popup.close()
        tryCompare(popup, "visible", false)
        compare(page.manualCoinSelection, false)
    }

    function test_custom_fee_send_uses_generic_confirmation_text() {
        const page = createTemporaryObject(sendComponent, testCase.Window.window.contentItem)
        verify(page !== null)
        page.width = testCase.width
        page.height = testCase.height
        page.visible = true
        testWalletModel.customFeeEnabled = true
        testWalletModel.customFeeRate = "4.2"
        // This fixture represents an already prepared explicit-rate review.
        testWalletTransaction.reviewFeeSource = FeeSelectionModel.Explicit
        page.openTransactionReview()
        const popup = findChild(page, "transactionReviewPopup")
        const reviewStack = findChild(popup, "sendTransactionReviewStack")
        tryCompare(popup, "opened", true)
        findChild(popup.contentItem, "sendTransactionReviewSendButton").clicked()
        tryCompare(reviewStack, "depth", 2)
        const completePage = findChild(reviewStack, "sendCompletePage")
        verify(completePage !== null)
        compare(completePage.targetBlocks, 0)
        compare(completePage.descriptionText,
                "Your node accepted the transaction. Confirmation depends on its fee rate.")
        popup.close()
    }

    function test_review_saves_psbt_without_leaving_modal() {
        const page = createTemporaryObject(sendComponent, testCase.Window.window.contentItem)
        verify(page !== null)
        page.width = testCase.width
        page.height = testCase.height
        page.visible = true
        page.openTransactionReview()
        const popup = findChild(page, "transactionReviewPopup")
        tryCompare(popup, "opened", true)
        const review = popup.contentItem
        const path = findChild(review, "sendTransactionReviewSavePsbtPathField")
        const saveButton = findChild(review, "sendTransactionReviewSaveButton")
        verify(path !== null)
        verify(saveButton !== null)
        path.text = "file:///tmp/review-test.psbt"
        saveButton.clicked()
        compare(testWalletModel.lastSavedPsbtPath, "file:///tmp/review-test.psbt")
        compare(popup.opened, true)
        popup.close()
    }

    function test_unverified_signer_export_keeps_the_review_and_captured_result() {
        const page = createTemporaryObject(sendComponent, testCase.Window.window.contentItem)
        page.width = testCase.width
        page.height = testCase.height
        page.visible = true
        page.openTransactionReview()
        const popup = findChild(page, "transactionReviewPopup")
        tryCompare(popup, "opened", true)
        const original = testWalletModel.currentTransaction
        testWalletModel.signerUnverifiedResultToken = "original-approval:review:view"
        try {
            const review = popup.contentItem
            const path = findChild(review, "sendTransactionReviewSavePsbtPathField")
            const button = findChild(review, "sendTransactionReviewExportUnverifiedSignerButton")
            verify(button !== null)
            tryCompare(button, "visible", true)
            path.text = "file:///tmp/unverified-signer-test.psbt"
            button.clicked()
            tryCompare(testWalletModel, "psbtActionBusy", false)
            compare(testWalletModel.lastSavedPsbtPath, "file:///tmp/unverified-signer-test.psbt")
            compare(testWalletModel.currentTransaction, original)
            compare(popup.opened, true)
        } finally {
            testWalletModel.signerUnverifiedResultToken = ""
            popup.close()
        }
    }

    function test_psbt_save_error_remains_visible_in_review() {
        const page = createTemporaryObject(sendComponent, testCase.Window.window.contentItem)
        verify(page !== null)
        page.width = testCase.width
        page.height = testCase.height
        page.visible = true
        page.openTransactionReview()
        const popup = findChild(page, "transactionReviewPopup")
        tryCompare(popup, "opened", true)
        const review = popup.contentItem
        const path = findChild(review, "sendTransactionReviewSavePsbtPathField")
        const saveButton = findChild(review, "sendTransactionReviewSaveButton")
        const error = findChild(review, "sendTransactionReviewSaveError")
        const toast = findChild(review, "sendTransactionReviewSaveToast")
        verify(path !== null)
        verify(saveButton !== null)
        verify(error !== null)
        verify(toast !== null)
        path.text = "file:///tmp/review-failed.psbt"
        testWalletModel.failNextPsbtSaveForTest = true
        saveButton.clicked()
        tryCompare(error, "visible", true)
        compare(error.text, "The original PSBT action could not be completed.")
        compare(toast.visible, false)
        compare(popup.opened, true)
    }

    function test_send_continue_button_tracks_recipient_validity() {
        const page = createTemporaryObject(sendComponent, this)
        verify(page !== null)

        const continueButton = findChild(page, "sendReviewButton")
        verify(continueButton !== null)

        testRecipientsModel.allValid = false
        tryCompare(continueButton, "enabled", false)

        testRecipientsModel.allValid = true
        tryCompare(continueButton, "enabled", true)
    }

    function test_send_continue_button_requires_fee_buffer() {
        const page = createTemporaryObject(sendComponent, this)
        verify(page !== null)

        const continueButton = findChild(page, "sendReviewButton")
        const prepareError = findChild(page, "sendPrepareTransactionErrorText")
        const prepareErrorText = findChild(page, "sendPrepareTransactionErrorText")
        verify(continueButton !== null)
        verify(prepareError !== null)
        verify(prepareErrorText !== null)

        testWalletModel.sendAmountExhaustsBalance = true

        tryCompare(continueButton, "enabled", false)
        tryCompare(page, "formErrorText", "Amount plus fee exceeds available balance. Some of your coins are locked.")
        compare(prepareErrorText.text, "Amount plus fee exceeds available balance. Some of your coins are locked.")

        testWalletModel.sendAmountExhaustsBalance = false

        tryCompare(continueButton, "enabled", true)
        tryCompare(page, "formErrorText", "")
        compare(prepareErrorText.text, "")

        testCoinsListModel.toggleCoinSelection(0)
        testWalletModel.sendAmountExhaustsBalance = true

        tryCompare(continueButton, "enabled", false)
        tryCompare(page, "formErrorText", "Selected inputs do not cover the amount plus fee")
        compare(prepareErrorText.text, "Selected inputs do not cover the amount plus fee")
    }

    function test_send_shows_recipient_validation_error() {
        const page = createTemporaryObject(sendComponent, this)
        verify(page !== null)

        const continueButton = findChild(page, "sendReviewButton")
        const prepareError = findChild(page, "sendPrepareTransactionErrorText")
        const prepareErrorText = findChild(page, "sendPrepareTransactionErrorText")
        verify(continueButton !== null)
        verify(prepareError !== null)
        verify(prepareErrorText !== null)

        testRecipientsModel.allValid = false
        testRecipientsModel.validationError = "Complete every recipient before continuing."

        tryCompare(continueButton, "enabled", false)
        tryCompare(page, "recipientValidationError", "Complete every recipient before continuing.")
        tryCompare(prepareErrorText, "text", "Complete every recipient before continuing.")
    }

    function test_send_prepare_transaction_success_and_failure_paths() {
        const page = createTemporaryObject(sendComponent, testCase.Window.window.contentItem,
            {width: testCase.width, height: testCase.height, visible: true})
        verify(page !== null)
        verify(page.visible)

        const continueButton = findChild(page, "sendReviewButton")
        const prepareError = findChild(page, "sendPrepareTransactionErrorText")
        const prepareErrorText = findChild(page, "sendPrepareTransactionErrorText")
        verify(continueButton !== null)
        verify(prepareError !== null)
        verify(prepareErrorText !== null)

        testSendRecipient.isValid = true
        const callsBefore = testWalletModel.prepareTransactionCalls
        compare(page.prepareTransactionErrorText, "")
        compare(prepareErrorText.text, "")

        testWalletModel.prepareTransactionResult = false
        continueButton.clicked()
        compare(testWalletModel.prepareTransactionCalls, callsBefore + 1)
        tryCompare(page, "prepareTransactionErrorText", "Amount plus fee exceeds available balance. Some of your coins are locked.")
        compare(prepareErrorText.text, "Amount plus fee exceeds available balance. Some of your coins are locked.")

        page.prepareTransactionErrorText = ""
        testCoinsListModel.toggleCoinSelection(0)
        continueButton.clicked()
        compare(testWalletModel.prepareTransactionCalls, callsBefore + 2)
        tryCompare(page, "prepareTransactionErrorText", "Selected inputs do not cover the amount plus fee")
        compare(prepareErrorText.text, "Selected inputs do not cover the amount plus fee")

        testWalletModel.prepareTransactionResult = true
        continueButton.clicked()
        compare(testWalletModel.prepareTransactionCalls, callsBefore + 3)
        tryCompare(findChild(page, "transactionReviewPopup"), "opened", true)
        compare(page.prepareTransactionErrorText, "")
        compare(prepareErrorText.text, "")
    }

    function test_send_amount_changes_schedule_live_fee_estimation() {
        const page = createTemporaryObject(sendComponent, this)
        verify(page !== null)

        const amountInput = findChild(page, "sendAmountInput")
        verify(amountInput !== null)

        verify(testSetup.replaceText(amountInput, ""))
        amountInput.forceActiveFocus()
        verify(amountInput.activeFocus)
        const callsBefore = testWalletModel.scheduleFeeEstimatesCalls
        keyClick("1")

        tryVerify(function() { return testWalletModel.scheduleFeeEstimatesCalls > callsBefore })
        compare(amountInput.text, "1")
        compare(testSendRecipient.amount.display, "1.00000000")
    }

    function test_finishing_unchanged_amount_does_not_restart_fee_estimation() {
        const page = createTemporaryObject(sendComponent, this)
        verify(page !== null)
        const amountInput = findChild(page, "sendAmountInput")
        const review = findChild(page, "sendReviewButton")
        amountInput.forceActiveFocus()
        amountInput.selectAll()
        const callsBeforeEdit = testWalletModel.scheduleFeeEstimatesCalls
        keyClick("1")
        compare(testSendRecipient.amount.display, "1.00000000")
        verify(testWalletModel.scheduleFeeEstimatesCalls > callsBeforeEdit)
        const callsBefore = testWalletModel.scheduleFeeEstimatesCalls
        review.forceActiveFocus()
        verify(!amountInput.activeFocus)
        compare(testWalletModel.scheduleFeeEstimatesCalls, callsBefore)
    }

    function test_send_amount_allows_editing_whole_part_before_decimal() {
        const page = createTemporaryObject(sendComponent, this)
        verify(page !== null)

        const amountInput = findChild(page, "sendAmountInput")
        verify(amountInput !== null)

        verify(testSetup.replaceText(amountInput, "0.00000000"))
        amountInput.cursorPosition = 1
        amountInput.forceActiveFocus()
        verify(amountInput.activeFocus)
        keyClick("1")

        compare(amountInput.text, "01.00000000")
        compare(testSendRecipient.amount.display, "1.00000000")

        amountInput.focus = false
        wait(0)
        compare(amountInput.activeFocus, false)
        compare(amountInput.text, "1.00000000")
    }

    function test_send_amount_rejects_extra_decimal_digits_while_editing() {
        const page = createTemporaryObject(sendComponent, this)
        verify(page !== null)

        const amountInput = findChild(page, "sendAmountInput")
        verify(amountInput !== null)

        testSendRecipient.amount.display = "0.12345678"
        tryCompare(amountInput, "text", "0.12345678")
        amountInput.cursorPosition = amountInput.text.length
        amountInput.forceActiveFocus()
        verify(amountInput.activeFocus)
        keyClick("9")

        compare(amountInput.text, "0.123456789")
        compare(testSendRecipient.amount.display, "0.12345678")
        verify(!testSendRecipient.amount.inputAcceptable)
        verify(testSendRecipient.amount.inputError.length > 0)

        amountInput.focus = false
        wait(0)
        compare(amountInput.activeFocus, false)
        compare(amountInput.text, "0.123456789")
    }

    function test_send_amount_rejects_extra_decimal_points_while_editing() {
        const page = createTemporaryObject(sendComponent, this)
        verify(page !== null)

        const amountInput = findChild(page, "sendAmountInput")
        verify(amountInput !== null)

        testSendRecipient.amount.display = "1.20000000"
        verify(testSetup.replaceText(amountInput, "1.2"))
        amountInput.cursorPosition = amountInput.text.length
        amountInput.forceActiveFocus()
        verify(amountInput.activeFocus)
        keyClick(".")

        compare(amountInput.text, "1.2.")
        verify(!testSendRecipient.amount.inputAcceptable)
        verify(testSendRecipient.amount.inputError.length > 0)
        compare(testSendRecipient.amount.display, "1.20000000")
    }

    function test_send_uri_import_schedules_fee_estimate() {
        const page = createTemporaryObject(sendComponent, testCase.Window.window.contentItem,
            {width: testCase.width, height: testCase.height, visible: true})
        verify(page !== null)
        verify(page.visible)

        const sendPage = findChild(page, "walletSendPage")
        verify(sendPage !== null)

        const callsBefore = testWalletModel.scheduleFeeEstimatesCalls
        sendPage.imports.importText(
            "bitcoin:bcrt1qdavt4j2sd7dlhqsavtnfxvzppw6k7qy97tmnu9?amount=0.02000000&label=uri-label",
            SendImportModel.Clipboard
        )

        tryVerify(function() { return sendPage.imports.operation && !sendPage.imports.operation.pending })
        confirmExplicitImport(sendPage)

        // The mocked amount input still emits one schedule request when the
        // imported amount updates the bound field. The explicit URI-import
        // Refreshing the send form should contribute one more call.
        tryVerify(function() { return testWalletModel.scheduleFeeEstimatesCalls > callsBefore })
        compare(testSendRecipient.address.address, "bcrt1qdavt4j2sd7dlhqsavtnfxvzppw6k7qy97tmnu9")
        compare(testSendRecipient.amount.display, "0.02000000")
        compare(testSendRecipient.paymentRequestLabel, "uri-label")
        compare(testSendRecipient.label, "")
        compare(sendPage.imports.status, "Payment request imported from clipboard")
        compare(sendPage.imports.statusIsError, false)
    }

    function test_send_uri_import_dialog_focuses_field_and_applies_on_return() {
        const page = createTemporaryObject(sendComponent, this)
        verify(page !== null)
        const sendPage = findChild(page, "walletSendPage")
        sendPage.openPaymentRequestImport()
        const popup = findChild(sendPage, "sendUriImportPopup")
        tryCompare(popup, "opened", true)
        const field = findChild(popup.contentItem, "sendUriImportInput")
        tryCompare(field, "activeFocus", true)
        compare(findChild(popup.contentItem, "sendUriImportApplyButton").enabled, false)

        field.text = "bitcoin:bcrt1qdavt4j2sd7dlhqsavtnfxvzppw6k7qy97tmnu9?amount=0.01000000"
        keyClick(Qt.Key_Return)
        tryCompare(popup, "opened", false)
        // Imports are asynchronous and applied after an explicit confirmation.
        tryVerify(function() { return sendPage.imports.operation && !sendPage.imports.operation.pending })
        confirmExplicitImport(sendPage)
        tryCompare(testSendRecipient.amount, "display", "0.01000000")
        tryCompare(sendPage.imports, "status", "Payment request imported from manual entry")
    }

    function test_send_payment_uri_paste_applies_from_each_recipient_field_data() {
        return [
            { tag: "address", inputObjectName: "sendAddressInput" },
            { tag: "label", inputObjectName: "sendNoteInput" },
            { tag: "amount", inputObjectName: "sendAmountInput" },
        ]
    }

    function test_plain_address_paste_into_send_field() {
        const page = createTemporaryObject(sendComponent, testCase.Window.window.contentItem,
            {width: testCase.width, height: testCase.height, visible: true})
        verify(page !== null)
        verify(page.visible)

        const input = findChild(page, "sendAddressInput")
        verify(input !== null)
        const address = "bcrt1q8rcpp66mqmtsw27mx7awa7yfdrpzytumsapkw4"
        testSendRecipient.address.setAddress("")
        Clipboard.currentText = address
        input.forceActiveFocus()
        verify(input.activeFocus)
        pasteShortcut()

        tryCompare(testSendRecipient.address, "address", address)

        const replacement = "bcrt1qreplacementaddress000000000000000000000"
        Clipboard.currentText = replacement
        input.selectAll()
        pasteShortcut()
        tryCompare(testSendRecipient.address, "address", replacement)
    }

    function test_plain_paste_applies_at_once_without_an_import() {
        const page = createTemporaryObject(sendComponent, testCase.Window.window.contentItem,
            {width: testCase.width, height: testCase.height, visible: true})
        const sendPage = findChild(page, "walletSendPage")
        const component = findChild(page, "sendAddressField")
        const input = component.field
        const original = "bcrt1qpastedaddress"
        input.selectAll()
        Clipboard.currentText = original
        component.paste(false)
        compare(testSendRecipient.address.address, original)
        compare(sendPage.imports.pending, false)
        compare(sendPage.imports.operation, null)
    }

    function test_uri_probe_is_fenced_by_draft_and_visibility_data() {
        return [{tag: "edited", action: 0}, {tag: "hidden", action: 1}, {tag: "opening-unavailable", action: 2}]
    }
    function test_uri_probe_is_fenced_by_draft_and_visibility(data) {
        const page = createTemporaryObject(sendComponent, testCase.Window.window.contentItem,
            {width: testCase.width, height: testCase.height, visible: true})
        const sendPage = findChild(page, "walletSendPage")
        const original = testSendRecipient.address.address
        BitcoinUri.holdPublications = true
        try {
            sendPage.imports.importText("bitcoin:bcrt1qstaleresult?amount=0.42", SendImportModel.Clipboard)
            verify(sendPage.imports.pending)
            if (data.action === 0) testSendRecipient.label = "new draft"
            else if (data.action === 1) page.visible = false
            else testWalletModel.available = false
            BitcoinUri.releasePublications()
            compare(testSendRecipient.address.address, original)
            compare(testSendRecipient.amount.satoshi, 0)
        } finally { BitcoinUri.releasePublications(); testWalletModel.available = true }
    }

    function test_long_leading_space_uri_is_probed_without_prefix_truncation_data() {
        return [{tag: "keyboard", menu: false}, {tag: "context-menu", menu: true}]
    }
    function test_long_leading_space_uri_is_probed_without_prefix_truncation(data) {
        const page = createTemporaryObject(sendComponent, testCase.Window.window.contentItem,
            {width: testCase.width, height: testCase.height, visible: true})
        const input = findChild(page, "sendAddressInput")
        Clipboard.currentText = " ".repeat(8192) + "BITCOIN:bcrt1qlongrequest?amount=0.07"
        input.forceActiveFocus()
        if (data.menu) {
            const addressField = findChild(page, "sendAddressField")
            verify(addressField !== null)
            const menu = addressField.contextMenu
            verify(menu !== null)
            verify(waitForRendering(page))
            mouseClick(input, input.width / 2, input.height / 2, Qt.RightButton)
            tryCompare(menu, "opened", true)
            findChild(menu, "sendAddressInputContextMenuPaste").triggered()
        } else pasteShortcut()
        tryCompare(testSendRecipient.address, "address", "bcrt1qlongrequest")
        compare(testSendRecipient.amount.satoshi, 7000000)
    }

    function test_address_is_read_only_while_a_payment_request_is_imported() {
        const page = createTemporaryObject(sendComponent, testCase.Window.window.contentItem,
            {width: testCase.width, height: testCase.height, visible: true})
        const input = findChild(page, "sendAddressInput")
        verify(input !== null)
        testSendRecipient.address.setAddress("")
        compare(input.readOnly, false)
        testRecipientsModel.importPending = true
        try {
            compare(input.readOnly, true)
            input.forceActiveFocus()
            tryCompare(input, "activeFocus", true)
            keyClick("b")
            compare(input.text, "")
            compare(testSendRecipient.address.address, "")
        } finally { testRecipientsModel.importPending = false }
        compare(input.readOnly, false)
    }

    function test_amount_is_read_only_while_a_payment_request_is_imported() {
        const page = createTemporaryObject(sendComponent, testCase.Window.window.contentItem,
            {width: testCase.width, height: testCase.height, visible: true})
        const input = findChild(page, "sendAmountInput")
        verify(input !== null)
        testSendRecipient.amount.clear()
        compare(input.readOnly, false)
        testRecipientsModel.importPending = true
        try {
            compare(input.readOnly, true)
            input.forceActiveFocus()
            tryCompare(input, "activeFocus", true)
            keyClick("5")
            compare(input.text, "")
            compare(testSendRecipient.amount.inputText, "")
            compare(testSendRecipient.amount.display, "")
        } finally { testRecipientsModel.importPending = false }
        compare(input.readOnly, false)
        keyClick("5")
        compare(testSendRecipient.amount.inputText, "5")
    }

    function test_overlong_ordinary_amount_paste_never_accepts_numeric_prefix() {
        const page = createTemporaryObject(sendComponent, testCase.Window.window.contentItem,
            {width: testCase.width, height: testCase.height, visible: true})
        const input = findChild(page, "sendAmountInput")
        const sendPage = findChild(page, "walletSendPage")
        input.selectAll()
        Clipboard.currentText = "0.1" + " ".repeat(126) + "junk"
        input.forceActiveFocus()
        pasteShortcut()
        tryCompare(sendPage.imports, "pending", false)
        verify(!testSendRecipient.amount.inputAcceptable)
        compare(testSendRecipient.amount.satoshi, 0)
        compare(testSendRecipient.amount.inputError, "Amount input exceeds the 128-character limit.")
    }

    function test_send_payment_uri_paste_applies_from_each_recipient_field(data) {
        const page = createTemporaryObject(sendComponent, testCase.Window.window.contentItem,
            {width: testCase.width, height: testCase.height, visible: true})
        verify(page !== null)
        verify(page.visible)

        const sendPage = findChild(page, "walletSendPage")
        const input = findChild(page, data.inputObjectName)
        const overwritePopup = findChild(page, "sendPaymentUriOverwritePopup")
        verify(sendPage !== null)
        verify(input !== null)
        verify(overwritePopup !== null)

        const address = "bcrt1q8rcpp66mqmtsw27mx7awa7yfdrpzytumsapkw4"
        testSendRecipient.address.setAddress(address, 0)
        Clipboard.currentText = "bitcoin:" + address + "?amount=100.00000000&label=Alice&message=Pay%20me"
        input.forceActiveFocus()
        verify(input.activeFocus)
        pasteShortcut()

        tryCompare(sendPage.imports, "pending", false)
        compare(overwritePopup.opened, false)
        tryCompare(testSendRecipient.address, "address", address)
        compare(testSendRecipient.amount.satoshi, 10000000000)
        compare(testSendRecipient.paymentRequestLabel, "Alice")
        compare(testSendRecipient.label, "")
        compare(findChild(page, "sendNoteInput").text, "")
        compare(sendPage.imports.message, "Pay me")
        compare(sendPage.imports.status, "Payment request imported from clipboard")
        compare(sendPage.imports.statusIsError, false)
    }

    function test_send_payment_request_details_data() {
        return [
            { tag: "address-only", params: "", note: "Existing note", payTo: "", message: "" },
            { tag: "amount-only", params: "?amount=0.02", note: "Existing note", payTo: "", message: "" },
            { tag: "empty-details", params: "?label=&message=", note: "Existing note", payTo: "", message: "" },
            { tag: "label-only", params: "?label=Tully", note: "Existing note", payTo: "Tully", message: "" },
            { tag: "message-only", params: "?message=River%20Crossing", note: "Existing note", payTo: "", message: "River Crossing" },
            { tag: "message-empty-label", params: "?label=&message=River%20Crossing", note: "Existing note", payTo: "", message: "River Crossing" },
            { tag: "label-empty-message", params: "?label=Tully&message=", note: "Existing note", payTo: "Tully", message: "" },
            { tag: "both-details", params: "?label=Tully&message=River%20Crossing", note: "Existing note", payTo: "Tully", message: "River Crossing" },
        ]
    }

    function test_send_payment_request_details(data) {
        testSendRecipient.label = "Existing note"
        const page = createTemporaryObject(sendComponent, testCase.Window.window.contentItem,
            {width: testCase.width, height: testCase.height, visible: true})
        verify(page !== null)
        const sendPage = findChild(page, "walletSendPage")
        const section = findChild(page, "sendPaymentRequestSection")
        verify(section !== null)
        compare(section.visible, false)

        sendPage.imports.importPaste(BitcoinUri.AddressField, "bitcoin:bcrt1qreplacement" + data.params, page.recipient)
        const overwrite = findChild(page, "sendPaymentUriOverwritePopup")
        tryCompare(sendPage.imports, "pending", false)
        compare(overwrite.opened, false)
        tryCompare(sendPage.imports, "pending", false)
        compare(testSendRecipient.hasPaymentRequest, true)
        compare(testSendRecipient.label, data.note)
        compare(findChild(page, "sendNoteInput").text, data.note)
        compare(section.visible, data.payTo.length > 0 || data.message.length > 0)
        const payTo = findChild(page, "sendPaymentRequestPayTo")
        const message = findChild(page, "sendPaymentRequestMessageText")
        compare(payTo.visible, data.payTo.length > 0)
        compare(payTo.value, data.payTo)
        compare(message.visible, data.message.length > 0)
        compare(message.value, data.message)
        compare(message.title, "Message")

        // The imported note remains editable without changing the public details.
        const noteField = findChild(page, "sendNoteField")
        noteField.text = "My edited note"
        noteField.textEdited()
        compare(testSendRecipient.label, "My edited note")
        compare(testSendRecipient.message, data.message)
        compare(testSendRecipient.paymentRequestLabel, data.payTo)

        // Importing another request without public details hides the section.
        sendPage.imports.importText("bitcoin:bcrt1qreplacement?amount=0.01", SendImportModel.Clipboard)
        tryVerify(function() { return sendPage.imports.operation && !sendPage.imports.operation.pending })
        confirmExplicitImport(sendPage)
        tryCompare(sendPage.imports, "pending", false)
        compare(section.visible, false)
        compare(testSendRecipient.label, "My edited note")
    }

    function test_send_payment_uri_paste_preserves_private_note_independently() {
        testSendRecipient.label = "My private note"
        const page = createTemporaryObject(sendComponent, testCase.Window.window.contentItem,
            {width: testCase.width, height: testCase.height, visible: true})
        const form = findChild(page, "walletSendPage")
        const popup = findChild(page, "sendPaymentUriOverwritePopup")
        const uri = "bitcoin:bcrt1qsendtoaddress?label=Tully&message=River%20Crossing"
        form.imports.importPaste(BitcoinUri.AddressField, uri, page.recipient)
        tryCompare(form.imports, "pending", false)
        compare(popup.opened, false)
        compare(testSendRecipient.label, "My private note")
        compare(findChild(page, "sendNoteInput").text, "My private note")
        compare(testSendRecipient.paymentRequestLabel, "Tully")
        compare(testSendRecipient.message, "River Crossing")
        form.imports.importPaste(BitcoinUri.AddressField, uri, page.recipient)
        tryCompare(form.imports, "pending", false)
        compare(popup.opened, false)
        compare(testSendRecipient.label, "My private note")
    }

    function test_uri_fixed_amount_replacing_equal_maximum_requires_consent_data() {
        return [{tag: "keep-maximum", confirm: false}, {tag: "replace-with-fixed", confirm: true}]
    }
    function test_uri_fixed_amount_replacing_equal_maximum_requires_consent(data) {
        const page = createTemporaryObject(sendComponent, testCase.Window.window.contentItem,
            {width: testCase.width, height: testCase.height, visible: true})
        const sendPage = findChild(page, "walletSendPage")
        const address = "bcrt1q8rcpp66mqmtsw27mx7awa7yfdrpzytumsapkw4"
        testSendRecipient.address.setAddress(address)
        testSendRecipient.amount.satoshi = 12345
        testSendRecipient.label = ""
        findChild(page, "sendUseMaximumButton").clicked()
        compare(testSendRecipient.amountMode, SendRecipient.Maximum)
        Clipboard.currentText = "bitcoin:" + address + "?amount=0.00012345"
        sendPage.imports.importPaste(BitcoinUri.AddressField, Clipboard.text(), page.recipient)
        const prompt = findChild(page, "sendPaymentUriOverwritePopup")
        tryCompare(prompt, "opened", true)
        compare(testSendRecipient.amount.satoshi, 12345)
        compare(testSendRecipient.amountMode, SendRecipient.Maximum)
        findChild(prompt.contentItem, data.confirm ? "sendPaymentUriOverwriteConfirmButton" : "sendPaymentUriOverwriteCancelButton").clicked()
        tryCompare(prompt, "opened", false)
        tryCompare(sendPage.imports, "pending", false)
        compare(testSendRecipient.amount.satoshi, 12345)
        compare(testSendRecipient.amountMode, data.confirm ? SendRecipient.Fixed : SendRecipient.Maximum)
    }

    function test_send_payment_uri_paste_confirms_overwriting_other_fields() {
        testSendRecipient.amount.satoshi = 100000000
        testSendRecipient.label = "existing label"

        const page = createTemporaryObject(sendComponent, testCase.Window.window.contentItem)
        verify(page !== null)
        page.width = testCase.width
        page.height = testCase.height
        page.visible = true

        const sendPage = findChild(page, "walletSendPage")
        const addressInput = findChild(page, "sendAddressInput")
        const overwritePopup = findChild(page, "sendPaymentUriOverwritePopup")
        verify(sendPage !== null)
        verify(addressInput !== null)
        verify(overwritePopup !== null)

        const uri = "bitcoin:bcrt1qreplacement?amount=0.02000000&label=replacement-label"
        Clipboard.currentText = uri
        sendPage.imports.importPaste(BitcoinUri.AddressField, Clipboard.text(), page.recipient)
        tryCompare(overwritePopup, "opened", true)

        verify(addressInput.text !== uri)
        compare(testSendRecipient.address.address, "bcrt1qsendtoaddress")
        compare(testSendRecipient.amount.satoshi, 100000000)
        compare(testSendRecipient.label, "existing label")

        const cancelButton = findChild(overwritePopup.contentItem, "sendPaymentUriOverwriteCancelButton")
        verify(cancelButton !== null)
        cancelButton.clicked()
        tryCompare(overwritePopup, "opened", false)
        compare(testSendRecipient.address.address, "bcrt1qsendtoaddress")
        compare(testSendRecipient.amount.satoshi, 100000000)
        compare(testSendRecipient.label, "existing label")

        sendPage.imports.importPaste(BitcoinUri.AddressField, Clipboard.text(), page.recipient)
        tryCompare(overwritePopup, "opened", true)
        const confirmButton = findChild(overwritePopup.contentItem, "sendPaymentUriOverwriteConfirmButton")
        verify(confirmButton !== null)
        confirmButton.clicked()
        tryCompare(overwritePopup, "opened", false)

        compare(testSendRecipient.address.address, "bcrt1qreplacement")
        compare(testSendRecipient.amount.satoshi, 2000000)
        compare(testSendRecipient.paymentRequestLabel, "replacement-label")
        compare(testSendRecipient.label, "existing label")
    }

    function test_send_payment_uri_model_label_is_not_reparsed() {
        const page = createTemporaryObject(sendComponent, testCase.Window.window.contentItem,
            {width: testCase.width, height: testCase.height, visible: true})
        verify(page !== null)
        verify(page.visible)

        const sendPage = findChild(page, "walletSendPage")
        const addressInput = findChild(page, "sendAddressInput")
        verify(sendPage !== null)
        verify(addressInput !== null)

        const address = "bcrt1q8rcpp66mqmtsw27mx7awa7yfdrpzytumsapkw4"
        const nestedUri = "bitcoin:" + address + "?amount=1.00000000"
        Clipboard.currentText = "bitcoin:" + address + "?label=" + encodeURIComponent(nestedUri)
        addressInput.forceActiveFocus()
        verify(addressInput.activeFocus)
        pasteShortcut()

        tryCompare(testSendRecipient.address, "address", address)
        tryCompare(sendPage.imports, "pending", false)
        compare(testSendRecipient.paymentRequestLabel, nestedUri)
        compare(testSendRecipient.label, "")
        compare(testSendRecipient.amount.satoshi, 0)
    }

    function test_send_payment_uri_paste_preserves_omitted_fields() {
        testSendRecipient.amount.satoshi = 100000000
        testSendRecipient.label = "existing label"

        const page = createTemporaryObject(sendComponent, testCase.Window.window.contentItem)
        verify(page !== null)
        const sendPage = findChild(page, "walletSendPage")
        const overwritePopup = findChild(page, "sendPaymentUriOverwritePopup")
        verify(sendPage !== null)
        verify(overwritePopup !== null)

        sendPage.imports.importPaste(BitcoinUri.AddressField, "bitcoin:bcrt1qreplacement", page.recipient)

        tryCompare(sendPage.imports, "pending", false)
        compare(overwritePopup.opened, false)
        compare(testSendRecipient.address.address, "bcrt1qreplacement")
        compare(testSendRecipient.amount.satoshi, 100000000)
        compare(testSendRecipient.label, "existing label")
    }

    function test_recipients_are_available_by_default_and_stack() {
        const page = createTemporaryObject(sendComponent, this, {width: 1000, height: 900})
        verify(page !== null)
        const add = findChild(page, "sendAddRecipientButton")
        verify(add !== null)
        compare(testRecipientsModel.count, 1)
        add.clicked()
        compare(testRecipientsModel.count, 2)
        tryCompare(findChild(page, "sendRecipientCard_0"), "expanded", false)
        tryCompare(findChild(page, "sendRecipientCard_1"), "expanded", true)
        findChild(page, "sendRemoveRecipient_1").clicked()
        compare(testRecipientsModel.count, 1)
        verify(findChild(page, "sendAddressInput") !== null)
    }

    function test_fee_presets_and_custom_rate_validation() {
        const page = createTemporaryObject(sendComponent, this)
        const review = findChild(page, "sendReviewButton")
        const fee = findChild(page, "feeSelectionControl")
        const picker = findChild(page, "feeSelectionPicker")
        function selectFee(index) {
            picker.open()
            picker.itemAtIndex(index).clicked()
        }
        compare(fee.currentTarget, 6)
        selectFee(0)
        compare(testWalletModel.targetBlocks, 2)
        selectFee(2)
        compare(testWalletModel.targetBlocks, 10)
        selectFee(3)
        compare(testWalletModel.customFeeEnabled, true)
        testWalletModel.customFeeRate = "0"
        compare(review.enabled, false)
        testWalletModel.customFeeRate = "0.099"
        compare(review.enabled, false)
        testWalletModel.customFeeRate = "0.1"
        tryCompare(review, "enabled", true)
        testWalletModel.customFeeRate = "2.5"
        tryCompare(review, "enabled", true)
        selectFee(1)
        compare(testWalletModel.customFeeEnabled, false)
        compare(testWalletModel.targetBlocks, 6)
    }

    function test_maximum_fills_immediately_without_warning() {
        const page = createTemporaryObject(sendComponent, testCase.Window.window.contentItem, {width: 1000, height: 900})
        testWalletModel.currentTransactionSweepsWallet = true
        findChild(page, "sendUseMaximumButton").clicked()
        compare(testWalletModel.useMaximumCalls, 1)
        compare(findChild(page, "sendReviewSweepAlert").opened, false)
    }

    function test_reviewing_sweep_requires_confirmation_regardless_of_maximum_action() {
        const page = createTemporaryObject(sendComponent, testCase.Window.window.contentItem, {width: 1000, height: 900})
        testWalletModel.sendDraftSweepsWallet = true
        testRecipientsModel.totalAmountSatoshi = 12345678
        testSendRecipient.amount.satoshi = 12345678
        const alert = findChild(page, "sendReviewSweepAlert")
        const review = findChild(page, "transactionReviewPopup")
        const prepareCalls = testWalletModel.prepareTransactionCalls
        const calls = testWalletModel.sendTransactionCalls
        const amount = testSendRecipient.amount.satoshi
        findChild(page, "sendReviewButton").clicked()
        tryCompare(alert, "opened", true)
        compare(alert.title, "Use all available funds?")
        compare(alert.message, "You’re preparing to send all available funds (" + testSendRecipient.amount.displayWithUnit + ") from this wallet. Do you want to proceed?")
        compare(review.opened, false)
        compare(testWalletModel.prepareTransactionCalls, prepareCalls)
        compare(testWalletModel.useMaximumCalls, 0)
        findChild(alert.contentItem, "sendReviewSweepCancelButton").clicked()
        tryCompare(alert, "visible", false)
        compare(review.opened, false)
        compare(testWalletModel.prepareTransactionCalls, prepareCalls)
        compare(testSendRecipient.amount.satoshi, amount)
        compare(testWalletModel.sendTransactionCalls, calls)
        findChild(page, "sendReviewButton").clicked()
        tryCompare(alert, "opened", true)
        findChild(alert.contentItem, "sendReviewSweepConfirmButton").clicked()
        tryCompare(review, "opened", true)
        compare(testWalletModel.prepareTransactionCalls, prepareCalls + 1)
        compare(testWalletModel.useMaximumCalls, 0)
        compare(testWalletModel.sendTransactionCalls, calls)
    }

    function test_reviewing_partial_send_opens_review_directly() {
        const page = createTemporaryObject(sendComponent, testCase.Window.window.contentItem)
        findChild(page, "sendReviewButton").clicked()
        compare(findChild(page, "sendReviewSweepAlert").opened, false)
        tryCompare(findChild(page, "transactionReviewPopup"), "opened", true)
    }

    function test_review_sweep_requires_confirmation_and_keeps_draft_on_cancel() {
        const review = createTemporaryObject(reviewComponent, testCase.Window.window.contentItem, {width: 1000, height: 900})
        testWalletModel.currentTransactionSweepsWallet = true
        const calls = testWalletModel.sendTransactionCalls
        const discardCalls = testWalletModel.discardCurrentTransactionCalls
        const alert = findChild(review, "sendSweepAlert")
        findChild(review, "sendTransactionReviewSendButton").clicked()
        tryCompare(alert, "opened", true)
        compare(alert.title, "Send all available funds?")
        compare(alert.message, "This will send all available funds (" + testWalletTransaction.amountAmount.displayWithUnit + ") from this wallet. Once confirmed, the transaction cannot be undone.")
        compare(testWalletModel.sendTransactionCalls, calls)
        findChild(alert.contentItem, "sendSweepCancelButton").clicked()
        tryCompare(alert, "visible", false)
        compare(testWalletModel.sendTransactionCalls, calls)
        compare(testWalletModel.discardCurrentTransactionCalls, discardCalls)
        findChild(review, "sendTransactionReviewSendButton").clicked()
        tryCompare(alert, "opened", true)
        findChild(alert.contentItem, "sendSweepConfirmButton").clicked()
        compare(testWalletModel.sendTransactionCalls, calls + 1)
    }

    function test_review_sweep_alert_closes_when_wallet_changes() {
        const review = createTemporaryObject(reviewComponent, testCase.Window.window.contentItem, {width: 1000, height: 900})
        testWalletModel.currentTransactionSweepsWallet = true
        const calls = testWalletModel.sendTransactionCalls
        const alert = findChild(review, "sendSweepAlert")
        findChild(review, "sendTransactionReviewSendButton").clicked()
        tryCompare(alert, "opened", true)
        review.wallet = null
        tryCompare(alert, "visible", false)
        compare(testWalletModel.sendTransactionCalls, calls)
    }

    // A confirmation for one payment draft must not survive the draft being
    // replaced, for example by File > Open URI.
    function test_review_sweep_alert_closes_when_draft_is_replaced() {
        const page = createTemporaryObject(sendComponent, testCase.Window.window.contentItem, {width: 1000, height: 900})
        testWalletModel.sendDraftSweepsWallet = true
        testRecipientsModel.totalAmountSatoshi = 200000000
        const alert = findChild(page, "sendReviewSweepAlert")
        const prepareCalls = testWalletModel.prepareTransactionCalls

        findChild(page, "sendReviewButton").clicked()
        tryCompare(alert, "opened", true)
        testRecipientsModel.totalAmountSatoshi = 10000000
        tryCompare(alert, "visible", false)
        compare(testWalletModel.prepareTransactionCalls, prepareCalls)

        // Switching off Use maximum is a different draft too.
        testWalletModel.useMaximum()
        findChild(page, "sendReviewButton").clicked()
        tryCompare(alert, "opened", true)
        // A maximum amount following fee estimates is not an edit.
        testRecipientsModel.totalAmountSatoshi = 199990000
        wait(50)
        compare(alert.visible, true)
        testWalletModel.clearMaximum()
        tryCompare(alert, "visible", false)
        compare(testWalletModel.prepareTransactionCalls, prepareCalls)
    }

    function test_stale_review_sweep_confirmation_is_ignored() {
        const page = createTemporaryObject(sendComponent, testCase.Window.window.contentItem, {width: 1000, height: 900})
        testWalletModel.sendDraftSweepsWallet = true
        testRecipientsModel.totalAmountSatoshi = 200000000
        const alert = findChild(page, "sendReviewSweepAlert")
        const prepareCalls = testWalletModel.prepareTransactionCalls
        findChild(page, "sendReviewButton").clicked()
        tryCompare(alert, "opened", true)
        const confirm = findChild(alert.contentItem, "sendReviewSweepConfirmButton")
        testSendRecipient.amount.satoshi = testSendRecipient.amount.satoshi + 1
        confirm.clicked()
        compare(testWalletModel.prepareTransactionCalls, prepareCalls)
        compare(findChild(page, "transactionReviewPopup").opened, false)
    }

    // A password prompt authorizes only the transaction it was opened for.
    // Importing another PSBT behind it closes it and rejects a submit.
    function test_send_password_prompt_rejects_replaced_transaction() {
        const review = createTemporaryObject(reviewComponent, testCase.Window.window.contentItem, {width: 1000, height: 900})
        const prompt = findChild(review, "sendTransactionPassphrasePopup")
        const replacement = testSecondWalletModel.currentTransaction
        verify(prompt !== null)
        verify(replacement !== testWalletModel.currentTransaction)
        const reviewed = testWalletModel.currentTransaction
        testWalletModel.sendTransactionResult = false
        testWalletModel.transactionNeedsUnlock = true
        const calls = testWalletModel.sendTransactionWithPassphraseCalls
        try {
            review.commitSend()
            tryCompare(prompt, "opened", true)
            // Signing status updates keep the same transaction object.
            testWalletModel.replaceCurrentTransactionForTest(reviewed)
            wait(50)
            compare(prompt.visible, true)

            testWalletModel.replaceCurrentTransactionForTest(replacement)
            tryCompare(prompt, "visible", false)

            testWalletModel.replaceCurrentTransactionForTest(reviewed)
            review.commitSend()
            tryCompare(prompt, "opened", true)
            prompt.reviewedTransaction = replacement
            prompt.submitted("password")
            compare(testWalletModel.sendTransactionWithPassphraseCalls, calls)
            tryCompare(prompt, "visible", false)

            review.commitSend()
            tryCompare(prompt, "opened", true)
            testWalletModel.sendTransactionResult = true
            prompt.submitted("password")
            compare(testWalletModel.sendTransactionWithPassphraseCalls, calls + 1)
        } finally {
            testWalletModel.replaceCurrentTransactionForTest(reviewed)
            testWalletModel.transactionNeedsUnlock = false
        }
    }

    function test_review_sweep_alert_closes_when_transaction_is_replaced() {
        const review = createTemporaryObject(reviewComponent, testCase.Window.window.contentItem, {width: 1000, height: 900})
        const reviewed = testWalletModel.currentTransaction
        testWalletModel.currentTransactionSweepsWallet = true
        const calls = testWalletModel.sendTransactionCalls
        const alert = findChild(review, "sendSweepAlert")
        try {
            findChild(review, "sendTransactionReviewSendButton").clicked()
            tryCompare(alert, "opened", true)
            testWalletModel.replaceCurrentTransactionForTest(testSecondWalletModel.currentTransaction)
            tryCompare(alert, "visible", false)
            compare(testWalletModel.sendTransactionCalls, calls)
        } finally {
            testWalletModel.replaceCurrentTransactionForTest(reviewed)
        }
    }

    function test_review_subset_sends_without_sweep_alert() {
        const review = createTemporaryObject(reviewComponent, testCase.Window.window.contentItem)
        const calls = testWalletModel.sendTransactionCalls
        findChild(review, "sendTransactionReviewSendButton").clicked()
        compare(findChild(review, "sendSweepAlert").opened, false)
        compare(testWalletModel.sendTransactionCalls, calls + 1)
    }

    function test_maximum_label_tracks_selection_and_no_fee_checkbox() {
        const page = createTemporaryObject(sendComponent, testCase.Window.window.contentItem, {width: 1000, height: 900})
        page.visible = true
        compare(findChild(page, "sendDeductFeeCheckbox"), null)
        const maximum = findChild(page, "sendUseMaximumButton")
        const status = findChild(page, "sendAmountStatusText")
        compare(maximum.text, "Use maximum")
        compare(maximum.enabled, true)
        compare(status.visible, false)
        maximum.clicked()
        compare(maximum.enabled, false)
        compare(status.text, "Sending maximum available")
        compare(status.visible, true)
        testSendRecipient.amount.satoshi = 1000
        compare(maximum.enabled, true)
        compare(status.visible, false)
        testCoinsListModel.toggleCoinSelection(0)
        compare(maximum.text, "Use maximum from selected coins")
        maximum.clicked()
        compare(maximum.enabled, false)
        compare(status.text, "Sending maximum from selected coins")
        testWalletModel.clearSelectedCoins()
        compare(maximum.enabled, true)
        compare(status.visible, false)
    }

    function test_coin_selection_body_fits_small_window_with_large_text() {
        const window = testCase.Window.window
        const originalHeight = window.height
        const originalScale = Theme.textScale
        window.height = 720
        Theme.textScale = 1.3
        try {
            const page = createTemporaryObject(sendComponent, window.contentItem, {width: 1000, height: 720})
            const popup = findChild(page, "coinSelectionPopup")
            popup.open()
            tryCompare(popup, "opened", true)
            verify(waitForRendering(popup.contentItem))
            const body = findChild(popup.contentItem, "coinSelectionBodyScroll")
            const footer = findChild(popup.contentItem, "coinSelectionDoneButton")
            verify(body.y + body.height <= footer.mapToItem(popup.contentItem, 0, 0).y)
            verify(findChild(body, "sendCoinsList").height >= body.availableHeight)
            popup.close()
        } finally {
            window.height = originalHeight
            Theme.textScale = originalScale
        }
    }

    function test_coin_selection_cancel_and_apply() {
        testSendRecipient.amount.satoshi = 0
        testWalletModel.feeEstimatePending = true
        const page = createTemporaryObject(sendComponent, testCase.Window.window.contentItem, {width: 1000, height: 900})
        const popup = findChild(page, "coinSelectionPopup")
        popup.open(); tryCompare(popup, "opened", true)
        const apply = findChild(popup.contentItem, "coinSelectionDoneButton")
        compare(apply.enabled, false)
        testCoinsListModel.toggleCoinSelection(0)
        tryCompare(apply, "enabled", true)
        popup.close(); tryCompare(popup, "visible", false)
        compare(testCoinsListModel.selectedCoinsCount, 0)
        popup.open(); tryCompare(popup, "opened", true)
        testCoinsListModel.toggleCoinSelection(0)
        apply.clicked(); tryCompare(popup, "visible", false)
        compare(testCoinsListModel.selectedCoinsCount, 1)
        popup.open(); tryCompare(popup, "opened", true)
        testCoinsListModel.toggleCoinSelection(1)
        popup.close(); tryCompare(popup, "visible", false)
        compare(testCoinsListModel.selectedCoinsCount, 1)
    }

    function test_review_popup_keeps_keyboard_focus_off_send_form() {
        const page = createTemporaryObject(sendComponent, testCase.Window.window.contentItem)
        page.width = testCase.width
        page.height = testCase.height
        page.visible = true
        const addressInput = findChild(page, "sendAddressInput")
        const reviewButton = findChild(page, "sendReviewButton")
        const originalAddress = addressInput.text
        reviewButton.forceActiveFocus()
        keyClick(Qt.Key_Space)

        const popup = findChild(page, "transactionReviewPopup")
        tryCompare(popup, "opened", true)
        const closeButton = findChild(popup.contentItem, "sendTransactionReviewCloseButton")
        tryCompare(closeButton, "activeFocus", true)
        for (let i = 0; i < 4; ++i) {
            keyClick(Qt.Key_Tab)
            let focusItem = testCase.Window.window.activeFocusItem
            let insideReview = false
            while (focusItem) {
                if (focusItem === popup.contentItem) insideReview = true
                focusItem = focusItem.parent
            }
            verify(insideReview)
        }
        keyClick(Qt.Key_A, Qt.ControlModifier)
        keyClick("x")
        compare(addressInput.text, originalAddress)

        keyClick(Qt.Key_Escape)
        tryCompare(popup, "opened", false)
        tryCompare(reviewButton, "activeFocus", true)
    }

    function test_fast_send_completion_before_popup_open_gets_done_focus() {
        // The completion must arrive while the popup's enter transition runs.
        Theme.motion.mode = "full"
        const page = createTemporaryObject(sendComponent, testCase.Window.window.contentItem)
        verify(page !== null)
        page.width = testCase.width
        page.height = testCase.height
        page.visible = true
        page.openTransactionReview()
        const popup = findChild(page, "transactionReviewPopup")
        const stack = popup.contentItem
        compare(popup.opened, false)
        verify(stack.currentItem !== null)
        stack.currentItem.transactionSent("fast-completion", WalletQmlModel.SubmissionAccepted, WalletQmlModel.SendRecordUnknown)
        tryVerify(function() { return stack.currentItem && stack.currentItem.objectName === "sendCompletePage" })
        tryCompare(popup, "opened", true)
        const done = findChild(stack.currentItem, "sendResultDoneButton")
        verify(done !== null)
        tryCompare(done, "activeFocus", true)
        keyClick(Qt.Key_Space)
        tryCompare(popup, "visible", false)
    }

    function test_locked_coin_confirmation_uses_identity_after_refresh() {
        const page = createTemporaryObject(sendComponent, testCase.Window.window.contentItem, {width: 1000, height: 900})
        const popup = findChild(page, "coinSelectionPopup")
        popup.open()
        tryCompare(popup, "opened", true)
        tryVerify(function() { return findChild(popup.contentItem, "coinSelectionItem_2") !== null })
        findChild(popup.contentItem, "coinSelectionItem_2").clicked()
        const alert = findChild(page, "lockedCoinSelectionAlert")
        tryCompare(alert, "opened", true)

        testCoinsListModel.prependCoinForTest()
        findChild(alert.contentItem, "lockedCoinSelectionConfirmButton").clicked()
        compare(testCoinsListModel.isCoinSelected("coin-3"), true)
        compare(testCoinsListModel.isCoinSelected("new-coin"), false)

        tryCompare(alert, "visible", false)
        testCoinsListModel.toggleCoinSelectionById("coin-3")
        findChild(popup.contentItem, "coinSelectionItem_3").clicked()
        tryCompare(alert, "opened", true)
        testCoinsListModel.removeCoinForTest("coin-3")
        findChild(alert.contentItem, "lockedCoinSelectionConfirmButton").clicked()
        compare(testCoinsListModel.isCoinSelected("new-coin"), false)
        compare(findChild(popup.contentItem, "sendCoinsList").errorText,
                "This coin is no longer available. Try again.")
        popup.close()
    }
    Component { id: amountPasteComponent; BitcoinAmountInputField { amount: testSendRecipient.amount; interceptPaste: true; property int pasteCalls: 0; onPaymentUriPasted: (text) => { pasteCalls += 1; amount.satoshi = 7000000 } } }

    function test_native_text_change_from_empty_draft() {
        const field = createTemporaryObject(amountPasteComponent, testCase.Window.window.contentItem,
            {width: testCase.width, visible: true})
        verify(field !== null)
        verify(field.visible)
        field.amount.clear()
        Clipboard.currentText = "bitcoin:bcrt1qexample?amount=0.07"
        verify(testSetup.replaceText(field.field, Clipboard.currentText))
        compare(field.pasteCalls, 1)
        compare(field.amount.satoshi, 7000000)
    }
    function test_send_password_closes_when_opening_context_ends_data() {
        return [{tag: "hidden", action: 0}, {tag: "changed-opening", action: 1}, {tag: "unloaded", action: 2}]
    }

    function test_send_password_closes_when_opening_context_ends(data) {
        const review = createTemporaryObject(reviewComponent, testCase.Window.window.contentItem,
            {width: 1000, height: 900, visible: true})
        const prompt = findChild(review, "sendTransactionPassphrasePopup")
        verify(prompt !== null)
        prompt.passphraseFieldObjectName = "reviewSendContextField"
        const field = findChild(prompt, "reviewSendContextField")
        verify(field !== null)
        testWalletModel.available = true
        prompt.reviewedTransaction = testWalletModel.currentTransaction
        prompt.open()
        tryCompare(prompt, "opened", true)
        field.text = "original-opening-password"
        const originalCalls = testWalletModel.sendTransactionWithPassphraseCalls
        const otherCalls = testSecondWalletModel.sendTransactionWithPassphraseCalls
        try {
            if (data.action === 0) review.visible = false
            else if (data.action === 1) review.wallet = testSecondWalletModel
            else testWalletModel.available = false
            tryCompare(prompt, "visible", false)
            compare(field.text, "")
            prompt.submitted("delayed-password")
            compare(testWalletModel.sendTransactionWithPassphraseCalls, originalCalls)
            compare(testSecondWalletModel.sendTransactionWithPassphraseCalls, otherCalls)
        } finally {
            testWalletModel.available = true
            prompt.close()
        }
    }

    function test_prepare_password_closes_when_opening_context_ends_data() {
        return [{tag: "hidden", action: 0}, {tag: "changed-opening", action: 1}, {tag: "unloaded", action: 2}]
    }

    function test_prepare_password_closes_when_opening_context_ends(data) {
        const page = createTemporaryObject(sendComponent, testCase.Window.window.contentItem,
            {width: 1000, height: 900, visible: true})
        verify(page !== null)
        const prompt = findChild(page, "reviewPassphrasePopup")
        const field = findChild(prompt, "reviewPassphraseField")
        verify(prompt !== null && field !== null)
        testWalletModel.available = true
        prompt.open()
        tryCompare(prompt, "opened", true)
        field.text = "original-opening-password"
        const originalCalls = testWalletModel.prepareTransactionCalls
        const otherCalls = testSecondWalletModel.prepareTransactionCalls
        try {
            if (data.action === 0) page.visible = false
            else if (data.action === 1) page.wallet = testSecondWalletModel
            else testWalletModel.available = false
            tryCompare(prompt, "visible", false)
            compare(field.text, "")
            prompt.submitted("delayed-password")
            compare(testWalletModel.prepareTransactionCalls, originalCalls)
            compare(testSecondWalletModel.prepareTransactionCalls, otherCalls)
        } finally {
            testWalletModel.available = true
            prompt.close()
        }
    }

    // Separate windows keep physical button clicks inside a complete viewport
    // without resizing the window used by the existing component cases.
    Component {
        id: controlledSendWindowComponent
        Window { width: 1000; height: 900; visible: false }
    }
    Component { id: transactionNavigationSpyComponent; SignalSpy { signalName: "viewTransactionInActivity" } }

    function createControlledSendPage() {
        // QtTest destroys temporary objects in creation order. Register the
        // page first so its bindings are torn down while its window is alive.
        const page = createTemporaryObject(sendComponent, testCase.Window.window.contentItem,
            {width: 1000, height: 900, visible: false})
        verify(page !== null)
        const window = createTemporaryObject(controlledSendWindowComponent, testCase)
        verify(window !== null)
        page.parent = window.contentItem
        page.visible = true
        window.show()
        verify(waitForRendering(page))
        return page
    }
    function controlledSendEditor(form) {
        // The delegate forwards paste intents with its live editorItem. Use
        // that same port, rather than the form's cached itemAt() convenience.
        const card = findChild(form, "sendRecipientCard_0")
        verify(card !== null)
        const editor = card.editorItem
        verify(editor !== null)
        compare(editor.recipient, testSendRecipient)
        return editor
    }
    function controlledPasteField(editor, name) {
        if (name === "amount") return editor.amountField
        if (name === "label") return editor.noteField
        return editor.addressField
    }
    function clickControlledReview(page) {
        const scroll = findChild(page, "sendFormScrollView")
        const button = findChild(page, "sendReviewButton")
        verify(scroll !== null && button !== null)
        verify(button.enabled)
        const flickable = scroll.contentItem
        flickable.contentY = Math.max(0, flickable.contentHeight - flickable.height)
        verify(waitForRendering(page))
        const center = button.mapToItem(page, button.width / 2, button.height / 2)
        verify(center.y >= 0 && center.y < page.height)
        mouseClick(button)
    }
    function test_controlled_preparation_projects_one_owned_outcome_data() {
        return [
            {tag: "ready", result: true, unlock: false, selected: false, error: "", expected: "review"},
            {tag: "unlock", result: false, unlock: true, selected: false, error: "", expected: "unlock"},
            {tag: "explicit-failure", result: false, unlock: false, selected: false, error: "Original preparation failed.", expected: "Original preparation failed."},
            {tag: "empty-failure-wallet", result: false, unlock: false, selected: false, error: "", expected: "Amount plus fee exceeds available balance. Some of your coins are locked."},
            {tag: "empty-failure-selection", result: false, unlock: false, selected: true, error: "", expected: "Selected inputs do not cover the amount plus fee"}
        ]
    }
    function test_controlled_preparation_projects_one_owned_outcome(data) {
        if (data.selected) testCoinsListModel.toggleCoinSelection(0)
        testWalletModel.prepareTransactionResult = data.result
        testWalletModel.holdPreparationForTest = true
        const page = createControlledSendPage()
        const review = findChild(page, "transactionReviewPopup")
        const prompt = findChild(page, "reviewPassphrasePopup")
        const before = testWalletModel.prepareTransactionCalls
        try {
            clickControlledReview(page)
            compare(testWalletModel.prepareTransactionCalls, before + 1)
            compare(testWalletModel.preparationBusy, true)
            compare(review.visible, false)
            compare(prompt.visible, false)
            // These are the mock's public result projections, delivered while
            // its accepted command remains pending, before terminal publication.
            testWalletModel.transactionError = data.error
            testWalletModel.transactionNeedsUnlock = data.unlock
            testWalletModel.finishPreparationForTest()
            compare(testWalletModel.preparationBusy, false)
            if (data.expected === "review") {
                tryCompare(review, "opened", true)
                compare(review.reviewWallet, testWalletModel)
                compare(prompt.visible, false)
            } else if (data.expected === "unlock") {
                tryCompare(prompt, "opened", true)
                compare(review.visible, false)
            } else {
                compare(page.prepareTransactionErrorText, data.expected)
                compare(findChild(page, "sendPrepareTransactionErrorText").text, data.expected)
                compare(review.visible, false)
                compare(prompt.visible, false)
            }
            testWalletModel.preparationChanged()
            compare(testWalletModel.prepareTransactionCalls, before + 1)
        } finally {
            testWalletModel.finishPreparationForTest()
            testWalletModel.holdPreparationForTest = false
            testWalletModel.transactionNeedsUnlock = false
            review.close()
            prompt.close()
        }
    }
    function test_controlled_preparation_drops_late_interest_data() {
        return [
            {tag: "hidden", change: "hidden"},
            {tag: "hidden-then-returned", change: "returned"},
            {tag: "edited-draft", change: "draft"},
            {tag: "replacement-opening", change: "opening"}
        ]
    }
    function test_controlled_preparation_drops_late_interest(data) {
        testWalletModel.holdPreparationForTest = true
        const page = createControlledSendPage()
        const review = findChild(page, "transactionReviewPopup")
        const originalCalls = testWalletModel.prepareTransactionCalls
        const replacementCalls = testSecondWalletModel.prepareTransactionCalls
        try {
            clickControlledReview(page)
            compare(testWalletModel.prepareTransactionCalls, originalCalls + 1)
            if (data.change === "hidden") page.visible = false
            else if (data.change === "returned") { page.visible = false; page.visible = true }
            else if (data.change === "draft") testSendRecipient.label = "replacement draft"
            else page.wallet = testSecondWalletModel
            testWalletModel.finishPreparationForTest()
            compare(testWalletModel.preparationState, WalletQmlModel.PreparationReady)
            compare(review.visible, false)
            compare(page.prepareTransactionErrorText, "")
            compare(testWalletModel.prepareTransactionCalls, originalCalls + 1)
            compare(testSecondWalletModel.prepareTransactionCalls, replacementCalls)
            if (data.change === "draft") compare(testSendRecipient.label, "replacement draft")
        } finally {
            testWalletModel.finishPreparationForTest()
            testWalletModel.holdPreparationForTest = false
            review.close()
        }
    }
    function test_stale_preparation_explains_why_review_did_not_open() {
        testWalletModel.holdPreparationForTest = true
        const page = createControlledSendPage()
        const review = findChild(page, "transactionReviewPopup")
        try {
            clickControlledReview(page)
            testWalletModel.finishPreparationStaleForTest()
            compare(testWalletModel.preparationState, WalletQmlModel.PreparationStale)
            compare(review.visible, false)
            compare(page.prepareTransactionErrorText, "Your balance or this transaction changed while it was being prepared. Review it again.")
        } finally {
            testWalletModel.finishPreparationForTest()
            testWalletModel.holdPreparationForTest = false
            review.close()
        }
    }
    function test_reentrant_preparation_keeps_two_view_interests_separate() {
        const secondView = createControlledSendPage()
        let allowSecondView = false
        let secondRequested = false
        function requestFromSecondView() {
            if (!allowSecondView || secondRequested || testWalletModel.preparationState !== WalletQmlModel.PreparationReady) return
            secondRequested = true
            secondView.prepareTransactionForReview()
        }
        // Reenter through a second view while the first view has explicitly
        // lost interest. The result must not depend on observer delivery order.
        testWalletModel.preparationChanged.connect(requestFromSecondView)
        const originalView = createControlledSendPage()
        const originalReview = findChild(originalView, "transactionReviewPopup")
        const secondReview = findChild(secondView, "transactionReviewPopup")
        const before = testWalletModel.prepareTransactionCalls
        testWalletModel.holdPreparationForTest = true
        try {
            clickControlledReview(originalView)
            compare(testWalletModel.prepareTransactionCalls, before + 1)
            originalView.visible = false
            allowSecondView = true
            testWalletModel.finishPreparationForTest()
            compare(secondRequested, true)
            compare(testWalletModel.prepareTransactionCalls, before + 2)
            compare(testWalletModel.preparationBusy, true)
            originalView.visible = true
            compare(originalReview.visible, false)
            compare(secondReview.visible, false)
            testWalletModel.finishPreparationForTest()
            tryCompare(secondReview, "opened", true)
            compare(secondReview.reviewWallet, testWalletModel)
            compare(originalReview.visible, false)
            compare(originalView.prepareTransactionErrorText, "")
            compare(testWalletModel.prepareTransactionCalls, before + 2)
        } finally {
            testWalletModel.preparationChanged.disconnect(requestFromSecondView)
            testWalletModel.finishPreparationForTest()
            testWalletModel.holdPreparationForTest = false
            originalReview.close()
            secondReview.close()
        }
    }
    function test_preparation_refusal_preserves_pending_psbt_intent() {
        const page = createControlledSendPage()
        const review = findChild(page, "transactionReviewPopup")
        testWalletModel.holdPsbtPublicationForTest = true
        try {
            page.requestPsbtImport("original-pending-review.psbt")
            compare(testWalletModel.psbtActionBusy, true)
            const originalImport = testWalletModel.psbtActionId
            const prepareCalls = testWalletModel.prepareTransactionCalls
            clickControlledReview(page)
            compare(page.prepareTransactionErrorText, "Another wallet action is still running.")
            compare(testWalletModel.prepareTransactionCalls, prepareCalls)
            compare(testWalletModel.psbtActionId, originalImport)
            compare(review.visible, false)
            testWalletModel.completePsbtWorkForTest()
            testWalletModel.finishPsbtPublicationForTest()
            tryCompare(review, "opened", true)
            compare(review.reviewWallet, testWalletModel)
            compare(testWalletModel.prepareTransactionCalls, prepareCalls)
        } finally {
            testWalletModel.holdPsbtPublicationForTest = false
            review.close()
        }
    }
    function test_controlled_psbt_import_routes_public_result_data() {
        return [
            {tag: "signable", result: WalletQmlModel.WalletCanSign, popup: "transactionReviewPopup"},
            {tag: "save-only", result: WalletQmlModel.WalletCannotSign, popup: "transactionReviewPopup"},
            {tag: "review-pending", result: WalletQmlModel.ReviewPending, popup: "transactionReviewPopup"},
            {tag: "already-known", result: WalletQmlModel.TransactionAlreadyKnown, popup: "knownPsbtTxPopup"},
            {tag: "unsupported", result: WalletQmlModel.PsbtUnsupported, popup: "unsupportedPsbtPopup"}
        ]
    }
    function test_controlled_psbt_import_routes_public_result(data) {
        const page = createControlledSendPage()
        const spy = createTemporaryObject(transactionNavigationSpyComponent, this, {target: page})
        testWalletModel.psbtImportResultForTest = data.result
        testWalletModel.holdPsbtPublicationForTest = true
        testWalletModel.importedPsbt.setMatchedTxidForTest("1".repeat(64))
        const prepareCalls = testWalletModel.prepareTransactionCalls
        try {
            page.requestPsbtImport("captured-public-result.psbt")
            const ticket = testWalletModel.psbtActionId
            testWalletModel.completePsbtWorkForTest()
            compare(testWalletModel.psbtActionBusy, true)
            compare(findChild(page, data.popup).visible, false)
            testWalletModel.importedPsbt.setMatchedTxidForTest("2".repeat(64))
            testWalletModel.finishPsbtPublicationForTest()
            compare(testWalletModel.psbtActionId, ticket)
            compare(testWalletModel.psbtImportResult, data.result)
            const popup = findChild(page, data.popup)
            tryCompare(popup, "opened", true)
            compare(testWalletModel.prepareTransactionCalls, prepareCalls)
            if (data.popup === "knownPsbtTxPopup") {
                compare(popup.txid, "1".repeat(64))
                mouseClick(findChild(popup.contentItem, "knownPsbtTxViewButton"))
                compare(spy.count, 1)
                compare(spy.signalArguments[0][0], "1".repeat(64))
            } else if (data.popup === "unsupportedPsbtPopup") {
                compare(popup.message, "This PSBT is not supported yet.")
                // The message is set just before opening; let the layout settle before clicking.
                verify(waitForItemPolished(popup.contentItem))
                mouseClick(findChild(popup.contentItem, "unsupportedPsbtPopupOkButton"))
                compare(spy.count, 0)
            } else {
                compare(popup.reviewWallet, testWalletModel)
                compare(popup.importedReview, true)
                compare(spy.count, 0)
                popup.close()
            }
            tryCompare(popup, "visible", false)
        } finally {
            testWalletModel.holdPsbtPublicationForTest = false
            findChild(page, "transactionReviewPopup").close()
            findChild(page, "knownPsbtTxPopup").close()
            findChild(page, "unsupportedPsbtPopup").close()
        }
    }
    function test_controlled_psbt_result_keeps_original_opening_data() {
        return [{tag: "replaced", change: "opening"}, {tag: "closed", change: "unavailable"}, {tag: "hidden-returned", change: "returned"}]
    }
    function test_controlled_psbt_result_keeps_original_opening(data) {
        const page = createControlledSendPage()
        const otherTicket = testSecondWalletModel.psbtActionId
        testWalletModel.holdPsbtPublicationForTest = true
        try {
            page.requestPsbtImport("old-opening-only.psbt")
            const originalTicket = testWalletModel.psbtActionId
            testWalletModel.completePsbtWorkForTest()
            if (data.change === "opening") page.wallet = testSecondWalletModel
            else if (data.change === "unavailable") testWalletModel.available = false
            else { page.visible = false; page.visible = true }
            testWalletModel.finishPsbtPublicationForTest()
            compare(testWalletModel.psbtActionId, originalTicket)
            compare(testSecondWalletModel.psbtActionId, otherTicket)
            compare(findChild(page, "transactionReviewPopup").visible, false)
            compare(findChild(page, "knownPsbtTxPopup").visible, false)
            compare(findChild(page, "unsupportedPsbtPopup").visible, false)
        } finally {
            testWalletModel.resetPsbtForTest()
            testWalletModel.available = true
        }
    }
    function test_uri_service_admission_refusal_preserves_draft_data() {
        return [{tag: "text", file: false}, {tag: "file", file: true}]
    }
    function test_uri_service_admission_refusal_preserves_draft(data) {
        const page = createControlledSendPage()
        const form = findChild(page, "walletSendPage")
        const address = testSendRecipient.address.address
        const applications = testSendRecipient.appliedPaymentRequestCalls
        BitcoinUri.admitRequestsForTest = false
        const calls = BitcoinUri.requestCallsForTest
        try {
            if (data.file) form.imports.importFile("original-payment-request.txt")
            else form.imports.importText("bitcoin:bcrt1q8rcpp66mqmtsw27mx7awa7yfdrpzytumsapkw4?amount=0.04", SendImportModel.Clipboard)
            compare(BitcoinUri.requestCallsForTest, calls + 1)
            compare(form.imports.pending, false)
            compare(form.imports.statusIsError, true)
            compare(form.imports.status, "Payment URI services are unavailable. Try again.")
            compare(form.imports.message, "")
            compare(testSendRecipient.address.address, address)
            compare(testSendRecipient.amount.satoshi, 0)
            compare(testSendRecipient.appliedPaymentRequestCalls, applications)
        } finally { BitcoinUri.resetRequestControlsForTest() }
    }
    function test_owned_uri_successor_projects_its_own_result_after_admission_refusal_data() {
        return [{tag: "applied", valid: true}, {tag: "rejected", valid: false}]
    }
    function test_owned_uri_successor_projects_its_own_result_after_admission_refusal(data) {
        const page = createControlledSendPage()
        const form = findChild(page, "walletSendPage")
        const originalAddress = testSendRecipient.address.address
        const applications = testSendRecipient.appliedPaymentRequestCalls
        const text = "bitcoin:bcrt1q8rcpp66mqmtsw27mx7awa7yfdrpzytumsapkw4?amount=0.07&message=Owned%20successor"
        BitcoinUri.admitRequestsForTest = false
        const calls = BitcoinUri.requestCallsForTest
        try {
            form.imports.importText(text, SendImportModel.ManualEntry)
            compare(BitcoinUri.requestCallsForTest, calls + 1)
            compare(form.imports.pending, false)
            compare(form.imports.statusIsError, true)
            compare(form.imports.status, "Payment URI services are unavailable. Try again.")
            compare(form.imports.message, "")
            compare(testSendRecipient.appliedPaymentRequestCalls, applications)
            BitcoinUri.admitRequestsForTest = true
            BitcoinUri.holdPublications = true
            form.imports.importText(data.valid ? text : "ordinary text", SendImportModel.ManualEntry)
            compare(BitcoinUri.requestCallsForTest, calls + 2)
            verify(form.imports.operation !== null)
            compare(form.imports.pending, true)
            compare(form.imports.statusIsError, false)
            compare(form.imports.status, "")
            compare(form.imports.message, "")
            compare(testSendRecipient.address.address, originalAddress)
            compare(testSendRecipient.appliedPaymentRequestCalls, applications)
            BitcoinUri.releasePublications()
            confirmExplicitImport(form)
            compare(form.imports.pending, false)
            compare(testSendRecipient.appliedPaymentRequestCalls, applications + (data.valid ? 1 : 0))
            compare(form.imports.statusIsError, !data.valid)
            compare(form.imports.status, data.valid ? "Payment request imported from manual entry" : "Not a valid Bitcoin payment URI.")
            compare(form.imports.message, data.valid ? "Owned successor" : "")
            compare(testSendRecipient.address.address, data.valid ? "bcrt1q8rcpp66mqmtsw27mx7awa7yfdrpzytumsapkw4" : originalAddress)
            compare(testSendRecipient.amount.satoshi, data.valid ? 7000000 : 0)
            BitcoinUri.releasePublications()
            compare(testSendRecipient.appliedPaymentRequestCalls, applications + (data.valid ? 1 : 0))
        } finally {
            BitcoinUri.resetRequestControlsForTest()
            BitcoinUri.releasePublications()
        }
    }
    function test_uri_paste_is_ignored_while_hidden_or_unavailable_data() {
        return [{tag: "hidden", hidden: true}, {tag: "unavailable", hidden: false}]
    }
    function test_uri_paste_is_ignored_while_hidden_or_unavailable(data) {
        const page = createControlledSendPage()
        const form = findChild(page, "walletSendPage")
        const original = testSendRecipient.address.address
        const applications = testSendRecipient.appliedPaymentRequestCalls
        if (data.hidden) page.visible = false
        else testWalletModel.available = false
        const calls = BitcoinUri.requestCallsForTest
        try {
            form.imports.importPaste(BitcoinUri.AddressField, "bitcoin:bcrt1q8rcpp66mqmtsw27mx7awa7yfdrpzytumsapkw4?amount=0.03", page.recipient)
            compare(BitcoinUri.requestCallsForTest, calls)
            compare(form.imports.pending, false)
            compare(testSendRecipient.address.address, original)
            compare(testSendRecipient.appliedPaymentRequestCalls, applications)
        } finally { testWalletModel.available = true }
    }
    function test_uri_probe_failure_releases_original_editor_data() {
        return [{tag: "address", field: "address"}, {tag: "amount", field: "amount"}, {tag: "note", field: "label"}]
    }
    function test_uri_probe_failure_releases_original_editor(data) {
        const page = createControlledSendPage()
        const form = findChild(page, "walletSendPage")
        const editor = controlledSendEditor(form)
        const field = controlledPasteField(editor, data.field)
        verify(field !== null)
        const original = testSendRecipient.address.address
        const note = testSendRecipient.label
        const applications = testSendRecipient.appliedPaymentRequestCalls
        BitcoinUri.holdPublications = true
        BitcoinUri.probeErrorForTest = "The captured request probe failed."
        Clipboard.currentText = "bitcoin:bcrt1q8rcpp66mqmtsw27mx7awa7yfdrpzytumsapkw4?amount=0.05&message=original"
        try {
            field.paste(false)
            compare(form.imports.pending, true)
            Clipboard.currentText = "unrelated replacement"
            BitcoinUri.releasePublications()
            compare(form.imports.pending, false)
            compare(form.imports.status, "The captured request probe failed.")
            compare(form.imports.statusIsError, true)
            compare(testSendRecipient.address.address, original)
            compare(testSendRecipient.amount.satoshi, 0)
            compare(testSendRecipient.label, note)
            compare(testSendRecipient.appliedPaymentRequestCalls, applications)
        } finally { BitcoinUri.resetRequestControlsForTest(); BitcoinUri.releasePublications() }
    }
    function test_literal_paste_applies_to_its_own_field_data() {
        return [
            {tag: "address", name: "address", text: "bcrt1q8rcpp66mqmtsw27mx7awa7yfdrpzytumsapkw4"},
            {tag: "amount", name: "amount", text: "0.02"},
            {tag: "note", name: "label", text: "Owned literal note"}
        ]
    }
    function test_literal_paste_applies_to_its_own_field(data) {
        const page = createControlledSendPage()
        const form = findChild(page, "walletSendPage")
        const editor = controlledSendEditor(form)
        const field = controlledPasteField(editor, data.name)
        verify(field !== null)
        field.field.forceActiveFocus()
        field.field.selectAll()
        Clipboard.currentText = data.text
        const applications = testSendRecipient.appliedPaymentRequestCalls
        field.paste(false)
        if (data.name === "address") compare(testSendRecipient.address.address, data.text)
        else if (data.name === "amount") compare(testSendRecipient.amount.satoshi, 2000000)
        else compare(testSendRecipient.label, data.text)
        compare(form.imports.operation, null)
        compare(testSendRecipient.appliedPaymentRequestCalls, applications)
        compare(form.imports.statusIsError, false)
    }
    function test_empty_clipboard_completes_paste_without_parser_data() {
        return [{tag: "address", field: "address"}, {tag: "amount", field: "amount"}, {tag: "note", field: "label"}]
    }
    function test_empty_clipboard_completes_paste_without_parser(data) {
        const page = createControlledSendPage()
        const form = findChild(page, "walletSendPage")
        const editor = controlledSendEditor(form)
        const field = controlledPasteField(editor, data.field)
        verify(field !== null)
        Clipboard.currentText = ""
        const before = BitcoinUri.requestCallsForTest
        const address = testSendRecipient.address.address
        const note = testSendRecipient.label
        const applications = testSendRecipient.appliedPaymentRequestCalls
        field.paste(false)
        compare(BitcoinUri.requestCallsForTest, before)
        compare(testSendRecipient.address.address, address)
        compare(testSendRecipient.label, note)
        compare(testSendRecipient.amount.satoshi, 0)
        compare(testSendRecipient.appliedPaymentRequestCalls, applications)
    }
    function test_controlled_uri_projection_has_one_original_effect_data() {
        return [
            {tag: "message", text: "bitcoin:bcrt1q8rcpp66mqmtsw27mx7awa7yfdrpzytumsapkw4?amount=0.07&message=Owned%20message", message: "Owned message", valid: true, file: false},
            {tag: "no-message", text: "bitcoin:bcrt1q8rcpp66mqmtsw27mx7awa7yfdrpzytumsapkw4?amount=0.07", message: "", valid: true, file: false},
            {tag: "invalid-text", text: "ordinary text", message: "", valid: false, file: false},
            {tag: "file-failure", text: "original-request-file.txt", message: "", valid: false, file: true}
        ]
    }
    function test_controlled_uri_projection_has_one_original_effect(data) {
        const page = createControlledSendPage()
        const form = findChild(page, "walletSendPage")
        const address = testSendRecipient.address.address
        const applications = testSendRecipient.appliedPaymentRequestCalls
        BitcoinUri.holdPublications = true
        try {
            if (data.file) form.imports.importFile(data.text)
            else form.imports.importText(data.text, SendImportModel.ManualEntry)
            compare(form.imports.pending, true)
            compare(testSendRecipient.appliedPaymentRequestCalls, applications)
            compare(testSendRecipient.address.address, address)
            BitcoinUri.releasePublications()
            confirmExplicitImport(form)
            compare(form.imports.pending, false)
            compare(testSendRecipient.appliedPaymentRequestCalls, applications + (data.valid ? 1 : 0))
            if (data.valid) {
                compare(testSendRecipient.address.address, "bcrt1q8rcpp66mqmtsw27mx7awa7yfdrpzytumsapkw4")
                compare(testSendRecipient.amount.satoshi, 7000000)
                compare(form.imports.message, data.message)
                compare(form.imports.status, "Payment request imported from manual entry")
                compare(form.imports.statusIsError, false)
            } else {
                compare(testSendRecipient.address.address, address)
                compare(testSendRecipient.amount.satoshi, 0)
                compare(form.imports.statusIsError, true)
                compare(form.imports.status, data.file ? "File parsing is unavailable in QML unit tests." : "Not a valid Bitcoin payment URI.")
            }
            BitcoinUri.releasePublications()
            compare(testSendRecipient.appliedPaymentRequestCalls, applications + (data.valid ? 1 : 0))
        } finally { BitcoinUri.releasePublications() }
    }
    function test_reentrant_uri_projection_cannot_finish_in_replacement_view_data() {
        return [{tag: "hidden", replace: false}, {tag: "different-opening", replace: true}]
    }
    function test_reentrant_uri_projection_cannot_finish_in_replacement_view(data) {
        const page = createControlledSendPage()
        const form = findChild(page, "walletSendPage")
        const replacement = testSecondWalletModel.recipients.current
        const otherAddress = replacement.address.address
        const otherAmount = replacement.amount.satoshi
        const otherNote = replacement.label
        const originalApplications = testSendRecipient.appliedPaymentRequestCalls
        const otherApplications = replacement.appliedPaymentRequestCalls
        let changed = false
        function loseInterest() {
            if (changed) return
            changed = true
            if (data.replace) page.wallet = testSecondWalletModel
            else page.visible = false
        }
        testSendRecipient.address.addressChanged.connect(loseInterest)
        BitcoinUri.holdPublications = true
        try {
            form.imports.importText("bitcoin:bcrt1qdavt4j2sd7dlhqsavtnfxvzppw6k7qy97tmnu9?amount=0.09&message=original", SendImportModel.Clipboard)
            BitcoinUri.releasePublications()
            confirmExplicitImport(form)
            compare(changed, true)
            compare(testSendRecipient.appliedPaymentRequestCalls, originalApplications + 1)
            compare(testSendRecipient.address.address, "bcrt1qdavt4j2sd7dlhqsavtnfxvzppw6k7qy97tmnu9")
            compare(replacement.appliedPaymentRequestCalls, otherApplications)
            compare(replacement.address.address, otherAddress)
            compare(replacement.amount.satoshi, otherAmount)
            compare(replacement.label, otherNote)
            compare(form.imports.status, "")
            compare(form.imports.operation, null)
            compare(form.imports.pending, false)
            const popup = findChild(page, "sendPaymentUriOverwritePopup")
            compare(popup.opened, false)
            tryCompare(popup, "visible", false)
        } finally {
            testSendRecipient.address.addressChanged.disconnect(loseInterest)
            BitcoinUri.releasePublications()
        }
    }

    property var sendGapSpies: []
    function trackSendGapSpy(component, target) {
        const spy = createTemporaryObject(component, testCase, {target: target})
        verify(spy !== null)
        sendGapSpies.push(spy)
        return spy
    }
    Component { id: sendPasteRequestSpy; SignalSpy { signalName: "paymentUriPasted" } }
    Component { id: sendListClearedSpy; SignalSpy { signalName: "listCleared" } }
    Component { id: sendSelectionSnapshotSpy; SignalSpy { signalName: "selectionSnapshotChanged" } }
    function revealSendControl(page, control) {
        verify(control !== null)
        const scroll = findChild(page, "sendFormScrollView").contentItem
        const y = control.mapToItem(page, 0, 0).y
        scroll.contentY = Math.max(0, Math.min(scroll.contentHeight - scroll.height,
                                             scroll.contentY + y - page.height / 2))
        verify(waitForRendering(page))
        const center = control.mapToItem(page, control.width / 2, control.height / 2)
        verify(center.y >= 0 && center.y < page.height)
    }
    function revealNestedScrollControl(control) {
        // Settle pending layouts so scroll positions use current content sizes.
        for (let pending = control.parent; pending; pending = pending.parent) verify(waitForItemPolished(pending))
        let ancestor = control.parent
        for (let depth = 0; ancestor && depth < 32; ++depth) {
            const flickable = ancestor as Flickable
            if (flickable) {
                const y = control.mapToItem(flickable.contentItem, 0, 0).y
                flickable.contentY = Math.max(0, Math.min(flickable.contentHeight - flickable.height,
                                                        y - flickable.height / 2))
                verify(waitForRendering(control))
                const center = control.mapToItem(flickable, control.width / 2, control.height / 2)
                verify(center.x >= 0 && center.x < flickable.width)
                verify(center.y >= 0 && center.y < flickable.height)
                return
            }
            ancestor = ancestor.parent
        }
        fail("Expected a scrollable ancestor for the review control")
    }
    function test_validation_echo_preserves_original_import_intent_data() {
        return [{tag: "explicit-derived-total", paste: false}, {tag: "paste-validation-revision", paste: true}]
    }
    function test_desktop_inspection_uses_one_original_source_data() {
        return [{tag: "accepted", action: "accept"}, {tag: "selection-during-handoff", action: "replace"},
                {tag: "hidden-before-handoff", action: "hide"}]
    }
    function test_desktop_inspection_uses_one_original_source(data) {
        const page = createControlledSendPage()
        const operation = BitcoinUri.requestImportText("bitcoin:bcrt1qsendtoaddress?amount=0.007&message=Desktop", null, null, page)
        tryCompare(operation, "state", PaymentImportOperation.Inspecting)
        const applications = testSendRecipient.appliedPaymentRequestCalls
        const replacementApplications = testSecondWalletModel.recipients.current.appliedPaymentRequestCalls
        const mailbox = createTemporaryObject(desktopMailboxComponent, page, {inspection: operation, beforeReturn: function(receiver) {
            compare(receiver, findChild(page, "walletSendPage").imports)
            if (data.action === "replace") page.wallet = testSecondWalletModel
        }})
        verify(mailbox !== null)
        if (data.action === "hide") page.visible = false
        const accepted = page.acceptDesktopImport(mailbox)
        if (data.action === "accept") {
            compare(accepted, true)
            compare(mailbox.takenCount, 1)
            confirmExplicitImport(findChild(page, "walletSendPage"))
            tryCompare(testSendRecipient, "appliedPaymentRequestCalls", applications + 1)
            compare(testSendRecipient.amount.satoshi, 700000)
            compare(testSendRecipient.message, "Desktop")
        } else {
            compare(accepted, false)
            compare(mailbox.takenCount, data.action === "hide" ? 0 : 1)
            compare(testSendRecipient.appliedPaymentRequestCalls, applications)
        }
        compare(testSecondWalletModel.recipients.current.appliedPaymentRequestCalls, replacementApplications)
        compare(testWalletModel.sendTransactionCalls, 0)
        compare(testSecondWalletModel.sendTransactionCalls, 0)
    }
    function test_validation_echo_preserves_original_import_intent(data) {
        const page = createControlledSendPage()
        const form = findChild(page, "walletSendPage")
        const editor = controlledSendEditor(form)
        const uri = "bitcoin:bcrt1qsendtoaddress?amount=0.07&message=Original"
        const revision = page.draftRevision
        const applications = testSendRecipient.appliedPaymentRequestCalls
        BitcoinUri.holdPublications = true
        try {
            if (data.paste) { Clipboard.currentText = uri; editor.addressField.paste(false) }
            else form.imports.importText(uri, SendImportModel.ManualEntry)
            const original = form.imports.operation
            verify(original !== null)
            if (data.paste) {
                testSendRecipient.validationRevision++
                testSendRecipient.address.formattedAddressChanged()
                testSendRecipient.amount.inputChanged()
            } else testRecipientsModel.totalAmountChanged() // Repeated cached projection, same semantic revision.
            compare(page.draftRevision, revision)
            compare(form.imports.operation, original)
            compare(testSendRecipient.appliedPaymentRequestCalls, applications)
            BitcoinUri.releasePublications()
            tryCompare(form.imports, "pending", false)
            compare(testSendRecipient.appliedPaymentRequestCalls, applications + 1)
            compare(testSendRecipient.amount.satoshi, 7000000)
            compare(testSendRecipient.message, "Original")
        } finally { BitcoinUri.releasePublications() }
    }

    function test_clipboard_offer_tracks_public_message_but_ignores_private_note_and_formatting() {
        testSendRecipient.address.setAddress("bcrt1qsendtoaddress")
        testSendRecipient.amount.satoshi = 700000
        testSendRecipient.paymentRequestLabel = "Invoice"
        testSendRecipient.message = "Original"
        testSendRecipient.label = "Private bookkeeping"
        const page = createControlledSendPage()
        const form = findChild(page, "walletSendPage")
        Clipboard.currentText = "BITCOIN:bcrt1qsendtoaddress?message=%4Friginal&amount=0.00700000&label=Invoice"
        tryVerify(function() { return form.imports.clipboardCheck && !form.imports.clipboardCheck.pending })
        compare(form.imports.clipboardOffered, false)
        testSendRecipient.label = "Changed private bookkeeping"
        tryVerify(function() { return form.imports.clipboardCheck && !form.imports.clipboardCheck.pending })
        compare(form.imports.clipboardOffered, false)
        Clipboard.currentText = "bitcoin:bcrt1qsendtoaddress?amount=0.007&label=Invoice&message=Changed"
        tryCompare(form.imports, "clipboardOffered", true)
        Clipboard.currentText = "bitcoin:bcrt1qsendtoaddress?amount=0.007&label=Invoice"
        tryCompare(form.imports, "clipboardOffered", true)
        testSendRecipient.message = ""
        tryCompare(form.imports, "clipboardOffered", false)
        compare(testSendRecipient.label, "Changed private bookkeeping")
    }

    function test_locked_wallet_allows_local_draft_import_without_unlocking() {
        const page = createControlledSendPage()
        const form = findChild(page, "walletSendPage")
        testWalletModel.isEncrypted = true
        testWalletModel.isLocked = true
        try {
            testSendRecipient.label = "Private locked note"
            form.imports.importPaste(BitcoinUri.AddressField, "bitcoin:bcrt1qsendtoaddress?amount=0.007&message=Public", page.recipient)
            tryCompare(form.imports, "pending", false)
            compare(testSendRecipient.amount.satoshi, 700000)
            compare(testSendRecipient.message, "Public")
            compare(testSendRecipient.label, "Private locked note")
            compare(testWalletModel.isLocked, true)
            compare(testWalletModel.sendTransactionCalls, 0)
        } finally { testWalletModel.isEncrypted = false; testWalletModel.isLocked = false }
    }

    function test_native_psbt_picker_keeps_original_opening_data() {
        return [{tag: "accepted", action: "accept"}, {tag: "replaced-opening", action: "replace"},
                {tag: "cancelled", action: "cancel"}, {tag: "edited-draft", action: "draft"},
                {tag: "changed-recipient", action: "recipient"}, {tag: "hidden-view", action: "hide"}]
    }
    function test_native_psbt_picker_keeps_original_opening(data) {
        const page = createControlledSendPage()
        const form = findChild(page, "walletSendPage")
        const picker = findChild(page, "sendPsbtOpenDialog")
        const menu = findChild(page, "sendOptionsPopup")
        const options = findChild(page, "sendOptionsButton")
        const original = testWalletModel.psbtActionId
        const replacement = testSecondWalletModel.psbtActionId
        BitcoinUri.holdPublications = true
        try {
            revealSendControl(page, options)
            mouseClick(options)
            tryCompare(menu, "opened", true)
            mouseClick(findChild(menu, "sendImportPsbtFromFileButton"))
            tryCompare(picker, "visible", true)
            const captured = picker.contextObject
            verify(captured !== null)
            compare(captured.state, PaymentImportOperation.Empty)
            compare(picker.openCalls, 1)
            if (data.action === "replace") page.wallet = testSecondWalletModel
            else if (data.action === "draft") testSendRecipient.label = "Changed while chooser open"
            else if (data.action === "recipient") testRecipientsModel.add()
            else if (data.action === "hide") page.visible = false
            if (data.action === "cancel") { picker.close(); picker.rejected() }
            else picker.selectAndAccept("file:///original%20opening.psbt")
            compare(picker.visible, false)
            compare(testWalletModel.psbtActionId, original)
            compare(testSecondWalletModel.psbtActionId, replacement)
            if (data.action === "accept") {
                compare(captured.selectedPath, "file:///original opening.psbt") // Qt hands over the decoded QUrl display form; Native owns path interpretation.
                verify(!captured.requestFile("successor.psbt"))
                compare(findChild(page, "transactionReviewPopup").visible, false)
                BitcoinUri.releasePublications()
                const review = findChild(page, "transactionReviewPopup")
                tryCompare(review, "opened", true)
                compare(review.reviewWallet, testWalletModel)
                compare(review.importedReview, true)
            } else {
                compare(captured.selectedPath, "")
                BitcoinUri.releasePublications()
                compare(findChild(page, "transactionReviewPopup").visible, false)
                if (data.action !== "cancel") verify(!captured.requestFile("stale.psbt"))
            }
        } finally {
            picker.close()
            BitcoinUri.releasePublications()
            form.imports.cancel()
            const review = findChild(page, "transactionReviewPopup")
            review.close()
            tryCompare(review, "visible", false)
        }
    }

    function test_password_preparation_submission_owns_one_outcome_data() {
        return [{tag: "ready", result: "ready"}, {tag: "failed", result: "failed"},
                {tag: "admission-refused", result: "refused"}]
    }
    function test_password_preparation_submission_owns_one_outcome(data) {
        const page = createControlledSendPage()
        const prompt = findChild(page, "reviewPassphrasePopup")
        const review = findChild(page, "transactionReviewPopup")
        const field = findChild(prompt, "reviewPassphraseField")
        const calls = testWalletModel.prepareTransactionCalls
        const otherCalls = testSecondWalletModel.prepareTransactionCalls
        testWalletModel.holdPreparationForTest = true
        testWalletModel.prepareTransactionResult = false
        try {
            clickControlledReview(page)
            testWalletModel.transactionNeedsUnlock = true
            testWalletModel.finishPreparationForTest()
            tryCompare(prompt, "opened", true)
            // Alert action buttons are named once the popup shows them.
            const confirm = findChild(prompt.contentItem, "reviewPassphraseConfirmButton")
            verify(confirm !== null)
            compare(testWalletModel.prepareTransactionCalls, calls + 1)
            compare(review.visible, false)
            const originalPreparation = testWalletModel.preparationId
            let importTicket = 0
            if (data.result === "refused") {
                testWalletModel.holdPsbtPublicationForTest = true
                page.requestPsbtImport("original-busy-import.psbt")
                importTicket = testWalletModel.psbtActionId
                compare(testWalletModel.psbtActionBusy, true)
            }
            testWalletModel.prepareTransactionResult = data.result === "ready"
            field.text = "password-for-original-opening"
            tryCompare(confirm, "enabled", true)
            mouseClick(confirm)
            compare(field.text, "")
            compare(testSecondWalletModel.prepareTransactionCalls, otherCalls)
            if (data.result === "refused") {
                compare(testWalletModel.prepareTransactionCalls, calls + 1)
                compare(testWalletModel.preparationId, originalPreparation)
                compare(testWalletModel.psbtActionId, importTicket)
                compare(testWalletModel.psbtActionBusy, true)
                compare(prompt.busy, false)
                compare(review.visible, false)
            } else {
                compare(testWalletModel.prepareTransactionCalls, calls + 2)
                compare(testWalletModel.preparationId, originalPreparation + 1)
                compare(testWalletModel.preparationBusy, true)
                compare(prompt.busy, true)
                compare(confirm.enabled, false)
                mouseClick(confirm)
                compare(testWalletModel.prepareTransactionCalls, calls + 2)
                compare(review.visible, false)
                testWalletModel.transactionNeedsUnlock = false
                testWalletModel.transactionError = data.result === "failed" ? "Original password retry failed." : ""
                testWalletModel.finishPreparationForTest()
                if (data.result === "ready") {
                    tryCompare(prompt, "visible", false)
                    tryCompare(review, "opened", true)
                    compare(review.reviewWallet, testWalletModel)
                } else {
                    compare(testWalletModel.preparationState, WalletQmlModel.PreparationFailed)
                    compare(prompt.opened, true)
                    compare(findChild(prompt, "reviewPassphraseErrorText").text, "Original password retry failed.")
                    compare(review.visible, false)
                }
            }
            compare(field.text, "")
        } finally {
            page.visible = false
            testWalletModel.finishPreparationForTest()
            testWalletModel.holdPreparationForTest = false
            testWalletModel.transactionNeedsUnlock = false
            testWalletModel.resetPsbtForTest()
            prompt.close(); review.close()
            tryCompare(prompt, "visible", false)
            tryCompare(review, "visible", false)
        }
    }
    function test_competing_send_intent_keeps_original_admission_data() {
        return [{tag: "preparation-busy", kind: "preparation"}, {tag: "URI-pending", kind: "uri"},
                {tag: "opening-unavailable", kind: "unavailable"}, {tag: "PSBT-busy", kind: "psbt"}]
    }
    function test_competing_send_intent_keeps_original_admission(data) {
        const page = createControlledSendPage()
        const form = findChild(page, "walletSendPage")
        const review = findChild(page, "transactionReviewPopup")
        const calls = testWalletModel.prepareTransactionCalls
        const applications = testSendRecipient.appliedPaymentRequestCalls
        const originalAddress = testSendRecipient.address.address
        testWalletModel.holdPreparationForTest = true
        try {
            if (data.kind === "preparation") {
                clickControlledReview(page)
                const ticket = testWalletModel.preparationId
                page.prepareTransactionForReview()
                compare(testWalletModel.preparationId, ticket)
                compare(testWalletModel.prepareTransactionCalls, calls + 1)
                compare(page.prepareTransactionErrorText, "")
                compare(review.visible, false)
                testWalletModel.finishPreparationForTest()
            } else if (data.kind === "psbt") {
                testWalletModel.holdPsbtPublicationForTest = true
                page.requestPsbtImport("accepted-original.psbt")
                const ticket = testWalletModel.psbtActionId
                testWalletModel.psbtImportResultForTest = WalletQmlModel.PsbtUnsupported
                page.requestPsbtImport("competing-replacement.psbt")
                compare(testWalletModel.psbtActionId, ticket)
                compare(testWalletModel.prepareTransactionCalls, calls)
                testWalletModel.completePsbtWorkForTest()
                testWalletModel.finishPsbtPublicationForTest()
                compare(testWalletModel.psbtImportResult, WalletQmlModel.WalletCanSign)
                compare(findChild(page, "unsupportedPsbtPopup").visible, false)
            } else {
                if (data.kind === "uri") {
                    BitcoinUri.holdPublications = true
                    form.imports.importText("bitcoin:bcrt1qsendtoaddress?amount=0.07", SendImportModel.ManualEntry)
                    compare(form.imports.pending, true)
                } else testWalletModel.available = false
                page.confirmTransactionReview()
                page.prepareTransactionForReview()
                compare(testWalletModel.prepareTransactionCalls, calls)
                compare(testSendRecipient.appliedPaymentRequestCalls, applications)
                compare(testSendRecipient.address.address, originalAddress)
                compare(review.visible, false)
                if (data.kind === "uri") {
                    BitcoinUri.releasePublications()
                    compare(testSendRecipient.appliedPaymentRequestCalls, applications + 1)
                    compare(testSendRecipient.amount.satoshi, 7000000)
                    compare(form.imports.pending, false)
                } else testWalletModel.available = true
                page.prepareTransactionForReview()
                compare(testWalletModel.prepareTransactionCalls, calls + 1)
                testWalletModel.finishPreparationForTest()
            }
            tryCompare(review, "opened", true)
            compare(review.reviewWallet, testWalletModel)
            compare(testWalletModel.prepareTransactionCalls, calls + (data.kind === "psbt" ? 0 : 1))
        } finally {
            page.visible = false
            BitcoinUri.releasePublications()
            testWalletModel.available = true
            testWalletModel.finishPreparationForTest()
            testWalletModel.holdPreparationForTest = false
            testWalletModel.resetPsbtForTest()
            review.close()
            tryCompare(review, "visible", false)
        }
    }
    function test_native_amount_commit_preserves_original_provenance_data() {
        const uri = "bitcoin:bcrt1qsendtoaddress?amount=0.07"
        const numeric = "0.1" + " ".repeat(520) + "junk"
        return [{tag: "matching-URI", clip: uri, current: uri, result: "apply"},
                {tag: "different-native-URI", clip: uri, current: "0.9bitcoin:other" + "x".repeat(520), result: "restore"},
                {tag: "long-numeric-rejected", clip: numeric, current: numeric, result: "reject"}]
    }
    function test_native_amount_commit_preserves_original_provenance(data) {
        testSendRecipient.amount.clear()
        const page = createControlledSendPage()
        const form = findChild(page, "walletSendPage")
        const editor = controlledSendEditor(form)
        const field = editor.amountField
        const spy = trackSendGapSpy(sendPasteRequestSpy, field)
        const applications = testSendRecipient.appliedPaymentRequestCalls
        const address = testSendRecipient.address.address
        const note = testSendRecipient.label
        Clipboard.currentText = data.clip
        BitcoinUri.holdPublications = true
        try {
            verify(testSetup.replaceText(field.field, data.current))
            compare(spy.count, data.result === "apply" ? 1 : 0)
            if (data.result === "apply") compare(spy.signalArguments[0][0], data.clip)
            compare(testSendRecipient.amount.satoshi, 0)
            compare(testSendRecipient.appliedPaymentRequestCalls, applications)
            BitcoinUri.releasePublications()
            tryCompare(form.imports, "pending", false)
            compare(testSendRecipient.appliedPaymentRequestCalls, applications + (data.result === "apply" ? 1 : 0))
            compare(testSendRecipient.address.address, address)
            compare(testSendRecipient.label, note)
            compare(Clipboard.currentText, data.clip)
            if (data.result === "apply") compare(testSendRecipient.amount.satoshi, 7000000)
            else {
                compare(testSendRecipient.amount.satoshi, 0)
                if (data.result === "reject") {
                    // The whole edit is refused; no numeric prefix is accepted.
                    compare(testSendRecipient.amount.inputState, BitcoinAmount.Invalid)
                    compare(testSendRecipient.amount.inputError, "Amount input exceeds the 128-character limit.")
                } else {
                    // A restored edit leaves the amount's input untouched.
                    compare(testSendRecipient.amount.inputText, "")
                }
            }
            BitcoinUri.releasePublications()
            compare(spy.count, data.result === "apply" ? 1 : 0)
        } finally { BitcoinUri.releasePublications() }
    }
    function test_replacement_intent_cancels_a_pending_uri_paste_data() {
        return [{tag: "manual-import", literal: false}, {tag: "literal-note", literal: true}]
    }
    function test_replacement_intent_cancels_a_pending_uri_paste(data) {
        const page = createControlledSendPage()
        const form = findChild(page, "walletSendPage")
        const editor = controlledSendEditor(form)
        const request = trackSendGapSpy(sendPasteRequestSpy, editor.addressField)
        const applications = testSendRecipient.appliedPaymentRequestCalls
        const originalAddress = testSendRecipient.address.address
        Clipboard.currentText = "bitcoin:bcrt1qdavt4j2sd7dlhqsavtnfxvzppw6k7qy97tmnu9?amount=0.07"
        BitcoinUri.holdPublications = true
        try {
            editor.addressField.paste(false)
            compare(request.count, 1)
            const pasted = form.imports.operation
            verify(pasted !== null)
            if (data.literal) {
                Clipboard.currentText = "original literal replacement"
                editor.noteField.field.forceActiveFocus()
                editor.noteField.paste(false)
            } else form.imports.importText("bitcoin:bcrt1q8rcpp66mqmtsw27mx7awa7yfdrpzytumsapkw4?amount=0.09", SendImportModel.ManualEntry)
            compare(pasted.state, PaymentImportOperation.Closed)
            compare(testSendRecipient.appliedPaymentRequestCalls, applications)
            BitcoinUri.releasePublications()
            confirmExplicitImport(form)
            compare(form.imports.pending, false)
            compare(testSendRecipient.appliedPaymentRequestCalls, applications + (data.literal ? 0 : 1))
            compare(testSendRecipient.address.address, data.literal ? originalAddress : "bcrt1q8rcpp66mqmtsw27mx7awa7yfdrpzytumsapkw4")
            compare(testSendRecipient.label, data.literal ? "original literal replacement" : "")
            compare(testSendRecipient.amount.satoshi, data.literal ? 0 : 9000000)
            BitcoinUri.releasePublications()
            compare(pasted.state, PaymentImportOperation.Closed)
            compare(testSendRecipient.appliedPaymentRequestCalls, applications + (data.literal ? 0 : 1))
        } finally { BitcoinUri.releasePublications() }
    }
    function test_overwrite_confirmation_rejects_unavailable_opening() {
        testSendRecipient.label = "original private note"
        testSendRecipient.amount.satoshi = 10000
        const page = createControlledSendPage()
        const form = findChild(page, "walletSendPage")
        const editor = controlledSendEditor(form)
        const popup = findChild(page, "sendPaymentUriOverwritePopup")
        const applications = testSendRecipient.appliedPaymentRequestCalls
        const prepareCalls = testWalletModel.prepareTransactionCalls
        const address = testSendRecipient.address.address
        Clipboard.currentText = "bitcoin:bcrt1qsendtoaddress?amount=0.07&message=replacement"
        editor.addressField.paste(false)
        tryCompare(popup, "opened", true)
        try {
            const confirm = findChild(popup.contentItem, "sendPaymentUriOverwriteConfirmButton")
            testWalletModel.available = false
            confirm.clicked() // A queued old confirmation cannot revive the retired Source.
            tryCompare(popup, "visible", false)
            compare(testSendRecipient.appliedPaymentRequestCalls, applications)
            compare(testSendRecipient.address.address, address)
            compare(testSendRecipient.amount.satoshi, 10000)
            compare(testSendRecipient.label, "original private note")
            compare(testWalletModel.prepareTransactionCalls, prepareCalls)
            compare(form.imports.pending, false)
        } finally { testWalletModel.available = true; popup.close() }
    }
    function test_committed_completion_keeps_original_draft_ownership_data() {
        return [{tag: "view-activity", replace: false}, {tag: "replacement-opening", replace: true}]
    }
    function test_committed_completion_keeps_original_draft_ownership(data) {
        const page = createControlledSendPage()
        const popup = findChild(page, "transactionReviewPopup")
        const navigation = trackSendGapSpy(transactionNavigationSpyComponent, page)
        const originalClear = trackSendGapSpy(sendListClearedSpy, testRecipientsModel)
        const otherRecipients = testSecondWalletModel.recipients
        otherRecipients.clearToFront()
        otherRecipients.add()
        const otherClear = trackSendGapSpy(sendListClearedSpy, otherRecipients)
        const otherRecipient = otherRecipients.current
        const otherAddress = otherRecipient.address.address
        const otherAmount = otherRecipient.amount.satoshi
        const otherNote = otherRecipient.label
        const calls = testWalletModel.sendTransactionCalls
        const otherCalls = testSecondWalletModel.sendTransactionCalls
        testCoinsListModel.setManualInputSelection(true)
        testCoinsListModel.toggleCoinSelection(0)
        testWalletModel.holdSendForTest = true
        try {
            page.openTransactionReview()
            tryCompare(popup, "opened", true)
            const sendButton = findChild(popup.contentItem, "sendTransactionReviewSendButton")
            verify(sendButton !== null && sendButton.visible && sendButton.enabled)
            revealNestedScrollControl(sendButton)
            mouseClick(sendButton)
            compare(testWalletModel.sendBusy, true)
            const stack = findChild(popup, "sendTransactionReviewStack")
            compare(stack.depth, 1)
            compare(navigation.count, 0)
            compare(originalClear.count, 0)
            testWalletModel.finishSendForTest()
            tryCompare(stack, "depth", 2)
            const complete = findChild(stack, "sendCompletePage")
            compare(complete.txid, testWalletTransaction.txid)
            if (data.replace) page.wallet = testSecondWalletModel
            else {
                const button = findChild(complete, "sendResultViewTransactionButton")
                tryCompare(button.parent, "opacity", 1)
                mouseClick(button)
            }
            tryCompare(popup, "visible", false)
            compare(navigation.count, data.replace ? 0 : 1)
            if (!data.replace) {
                compare(navigation.signalArguments[0][0], testWalletTransaction.txid)
                compare(testCoinsListModel.selectedCoinsCount, 0)
                compare(page.manualCoinSelection, false)
            }
            compare(originalClear.count, data.replace ? 0 : 1)
            compare(popup.sent, false)
            compare(otherClear.count, 0)
            compare(otherRecipients.count, 2)
            compare(otherRecipient.address.address, otherAddress)
            compare(otherRecipient.amount.satoshi, otherAmount)
            compare(otherRecipient.label, otherNote)
            compare(testWalletModel.sendTransactionCalls, calls + 1)
            compare(testSecondWalletModel.sendTransactionCalls, otherCalls)
        } finally {
            testWalletModel.finishSendForTest()
            testWalletModel.resetSendForTest()
            popup.close()
            tryCompare(popup, "visible", false)
            otherRecipients.clearToFront()
        }
    }
    function test_recipient_header_preserves_draft_when_expanded_and_collapsed() {
        testRecipientsModel.add()
        const page = createControlledSendPage()
        const first = findChild(page, "sendRecipientCard_0")
        const second = findChild(page, "sendRecipientCard_1")
        const header = findChild(first, "sendEditRecipient_0")
        const address = testSendRecipient.address.address
        const amount = testSendRecipient.amount.satoshi
        const note = testSendRecipient.label
        compare(first.expanded, false)
        compare(second.expanded, true)
        compare(first.editorItem, null)
        revealSendControl(page, header)
        mouseClick(header)
        compare(testRecipientsModel.currentIndex, 1)
        compare(first.expanded, true)
        compare(second.expanded, false)
        verify(first.editorItem !== null)
        compare(second.editorItem, null)
        mouseClick(header)
        compare(first.expanded, false)
        compare(second.expanded, false)
        compare(first.editorItem, null)
        compare(testRecipientsModel.count, 2)
        compare(testSendRecipient.address.address, address)
        compare(testSendRecipient.amount.satoshi, amount)
        compare(testSendRecipient.label, note)
    }
    function test_coin_picker_clears_only_a_real_selection_data() {
        return [{tag: "no-inputs", selected: false}, {tag: "selected-input", selected: true}]
    }
    function test_manual_empty_selection_is_explicit_and_blocks_review() {
        const page = createControlledSendPage()
        const picker = findChild(page, "sendCoinControlPicker")
        const manual = findChild(picker, "sendCoinControlPickerOption_1")
        const automatic = findChild(picker, "sendCoinControlPickerOption_0")
        const review = findChild(page, "sendReviewButton")
        revealSendControl(page, picker)
        mouseClick(manual)
        compare(page.manualCoinSelection, true)
        compare(picker.currentIndex, 1)
        compare(testWalletModel.coinSelectionBlocked, true)
        verify(!review.enabled)
        compare(findChild(page, "sendInputsSelectedText").text, "No inputs selected")

        testCoinsListModel.toggleCoinSelection(0)
        compare(testCoinsListModel.selectedCoinsCount, 1)
        compare(testWalletModel.coinSelectionBlocked, false)
        verify(review.enabled)

        mouseClick(automatic)
        compare(page.manualCoinSelection, false)
        compare(testCoinsListModel.selectedCoinsCount, 0)
        compare(testWalletModel.coinSelectionBlocked, false)
        verify(review.enabled)
    }

    function test_missing_manual_input_blocks_review_until_explicit_reset() {
        const page = createControlledSendPage()
        const picker = findChild(page, "sendCoinControlPicker")
        const manual = findChild(picker, "sendCoinControlPickerOption_1")
        const review = findChild(page, "sendReviewButton")
        revealSendControl(page, picker)
        mouseClick(manual)
        testCoinsListModel.toggleCoinSelection(0)
        verify(review.enabled)
        testCoinsListModel.setSelectionReconciliationRequiredForTest(true)
        compare(testWalletModel.coinSelectionBlocked, true)
        verify(!review.enabled)
        verify(page.formErrorText.indexOf("no longer available") >= 0)

        testCoinsListModel.setSelectionReconciliationRequiredForTest(false)
        compare(testWalletModel.coinSelectionBlocked, false)
        verify(review.enabled)
    }

    function test_coin_picker_clears_only_a_real_selection(data) {
        const page = createControlledSendPage()
        const picker = findChild(page, "sendCoinControlPicker")
        const manual = findChild(picker, "sendCoinControlPickerOption_1")
        const automatic = findChild(picker, "sendCoinControlPickerOption_0")
        const address = testSendRecipient.address.address
        const amount = testSendRecipient.amount.satoshi
        revealSendControl(page, picker)
        mouseClick(manual)
        compare(page.manualCoinSelection, true)
        compare(picker.currentIndex, 1)
        if (data.selected) testCoinsListModel.toggleCoinSelection(0)
        const selected = trackSendGapSpy(sendSelectionSnapshotSpy, testCoinsListModel)
        const fees = testWalletModel.scheduleFeeEstimatesCalls
        mouseClick(automatic)
        compare(page.manualCoinSelection, false)
        compare(picker.currentIndex, 0)
        compare(testCoinsListModel.selectedCoinsCount, 0)
        compare(selected.count, 2)
        compare(testWalletModel.scheduleFeeEstimatesCalls, fees + 2)
        compare(testSendRecipient.address.address, address)
        compare(testSendRecipient.amount.satoshi, amount)
    }
    function test_clipboard_probe_cannot_retarget_unavailable_opening_data() {
        return [{tag: "late-original-result", changeClipboard: false},
                {tag: "new-clipboard-while-unavailable", changeClipboard: true}]
    }
    function test_clipboard_probe_cannot_retarget_unavailable_opening(data) {
        const originalUri = "bitcoin:bcrt1qdavt4j2sd7dlhqsavtnfxvzppw6k7qy97tmnu9?amount=0.07"
        const replacementUri = "bitcoin:bcrt1q8rcpp66mqmtsw27mx7awa7yfdrpzytumsapkw4?amount=0.09"
        Clipboard.currentText = originalUri
        BitcoinUri.holdPublications = true
        const page = createControlledSendPage()
        const form = findChild(page, "walletSendPage")
        const button = findChild(page, "clipboardUriPasteButton")
        const applications = testSendRecipient.appliedPaymentRequestCalls
        const address = testSendRecipient.address.address
        try {
            const requests = BitcoinUri.requestCallsForTest
            verify(requests > 0)
            testWalletModel.available = false
            if (data.changeClipboard) {
                Clipboard.currentText = replacementUri
                compare(BitcoinUri.requestCallsForTest, requests)
            }
            BitcoinUri.releasePublications()
            compare(button.visible, false)
            compare(testSendRecipient.appliedPaymentRequestCalls, applications)
            compare(testSendRecipient.address.address, address)
            compare(testSendRecipient.amount.satoshi, 0)
            testWalletModel.available = true
            BitcoinUri.holdPublications = true
            page.visible = false
            page.visible = true
            compare(BitcoinUri.requestCallsForTest, requests + 1)
            BitcoinUri.releasePublications()
            tryCompare(button, "visible", true)
            revealSendControl(page, button)
            mouseClick(button)
            tryCompare(testSendRecipient.address, "address", data.changeClipboard ? "bcrt1q8rcpp66mqmtsw27mx7awa7yfdrpzytumsapkw4" : "bcrt1qdavt4j2sd7dlhqsavtnfxvzppw6k7qy97tmnu9")
            compare(testSendRecipient.amount.satoshi, data.changeClipboard ? 9000000 : 7000000)
            compare(testSendRecipient.appliedPaymentRequestCalls, applications + 1)
            compare(form.imports.pending, false)
        } finally { testWalletModel.available = true; BitcoinUri.releasePublications() }
    }

}

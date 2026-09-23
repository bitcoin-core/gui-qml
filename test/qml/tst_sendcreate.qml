// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.2
import org.bitcoincore.qt 1.0
import "../../qml/pages/wallet"

TestCase {
    id: testCase
    name: "SendCreate"
    when: windowShown
    width: 900
    height: 700

    Component {
        id: sendComponent

        SendCreate {}
    }

    Component {
        id: reviewComponent

        SendTransactionReview { wallet: testWalletModel }
    }

    Component {
        id: outcomeSpyComponent
        SignalSpy {}
    }

    function init() {
        walletController.setSelectedWallet("send-create-test-wallet")
        testWalletModel.sendDraftSweepsWallet = false
        testWalletModel.currentTransactionSweepsWallet = false
        testWalletModel.useMaximumCalls = 0
        testWalletModel.clearMaximum()
        testWalletModel.sendTransactionResult = true
        testWalletModel.feeEstimatePending = false
        testWalletModel.customFeeEnabled = false
        testWalletModel.customFeeRate = ""
        testWalletModel.targetBlocks = 6
        testWalletModel.prepareTransactionResult = true
        testWalletModel.sendAmountExhaustsBalance = false
        testWalletModel.currentTransactionCanSend = true
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
        optionsModel.displayUnit = BitcoinAmount.BTC
        testCoinsListModel.reset()
        Clipboard.currentText = ""
    }

    function pasteShortcut() {
        keyClick(Qt.Key_V, Qt.ControlModifier)
    }

    function makeVisibleSendPage() {
        const page = createTemporaryObject(sendComponent, testCase.Window.window.contentItem)
        verify(page !== null)
        page.width = testCase.width
        page.height = testCase.height
        page.visible = true
        return page
    }

    function test_incoming_payment_request_is_reviewed_before_it_is_applied() {
        const page = makeVisibleSendPage()
        const sendPage = findChild(page, "walletSendPage")
        const review = findChild(page, "sendPaymentRequestReviewPopup")
        verify(sendPage !== null)
        verify(review !== null)

        const address = "bcrt1qreviewaddress"
        const uri = "bitcoin:" + address + "?amount=0.02000000&label=Alice&message=Pay%20me"
        page.applyPaymentRequest(uri, "command line")
        tryCompare(review, "opened", true)

        compare(review.address, address)
        compare(review.hasAmount, true)
        compare(review.amountSatoshi, 2000000)
        compare(review.requestLabel, "Alice")
        compare(review.requestMessage, "Pay me")
        compare(review.source, "command line")
        // The form still holds what it had: nothing is applied before Apply.
        compare(testSendRecipient.address.address, "bcrt1qsendtoaddress")
        compare(testSendRecipient.label, "")

        const applyButton = findChild(review, "paymentRequestReviewApplyButton")
        verify(applyButton !== null)
        applyButton.clicked()
        tryCompare(review, "opened", false)

        compare(testSendRecipient.address.address, address)
        compare(testSendRecipient.amount.satoshi, 2000000)
        compare(testSendRecipient.paymentRequestLabel, "Alice")
        compare(sendPage.paymentRequestStatus, "Payment request imported from command line")
    }

    function test_discarded_payment_request_leaves_the_form_untouched() {
        testSendRecipient.amount.satoshi = 100000000
        testSendRecipient.label = "existing label"

        const page = makeVisibleSendPage()
        const review = findChild(page, "sendPaymentRequestReviewPopup")
        verify(review !== null)

        page.applyPaymentRequest("bitcoin:bcrt1qother?amount=0.03000000&label=Bob", "command line")
        tryCompare(review, "opened", true)
        // The request replaces populated fields, so the review says so.
        compare(review.replacesValues, true)

        const discardButton = findChild(review, "paymentRequestReviewDiscardButton")
        verify(discardButton !== null)
        discardButton.clicked()
        tryCompare(review, "opened", false)

        compare(testSendRecipient.address.address, "bcrt1qsendtoaddress")
        compare(testSendRecipient.amount.satoshi, 100000000)
        compare(testSendRecipient.label, "existing label")
    }

    function test_review_takes_keyboard_focus_from_the_form() {
        const page = makeVisibleSendPage()
        const review = findChild(page, "sendPaymentRequestReviewPopup")
        const noteInput = findChild(page, "sendNoteInput")
        verify(review !== null)
        verify(noteInput !== null)

        noteInput.forceActiveFocus()
        verify(noteInput.activeFocus)

        page.applyPaymentRequest("bitcoin:bcrt1qreviewaddress?label=Alice", "command line")
        tryCompare(review, "opened", true)
        verify(review.activeFocus)
        verify(!noteInput.activeFocus)

        keyClick(Qt.Key_X)
        compare(noteInput.text, "")

        keyClick(Qt.Key_Escape)
        tryCompare(review, "opened", false)
        compare(testSendRecipient.address.address, "bcrt1qsendtoaddress")
    }

    function test_page_does_not_accept_a_request_while_one_is_under_review() {
        const page = makeVisibleSendPage()
        const review = findChild(page, "sendPaymentRequestReviewPopup")
        verify(review !== null)

        compare(page.canAcceptPaymentRequest(), true)

        page.applyPaymentRequest("bitcoin:bcrt1qreviewaddress?label=Alice", "command line")
        tryCompare(review, "opened", true)
        compare(page.canAcceptPaymentRequest(), false)

        findChild(review, "paymentRequestReviewDiscardButton").clicked()
        tryCompare(review, "opened", false)
        compare(page.canAcceptPaymentRequest(), true)
    }

    function test_review_with_a_long_message_keeps_its_actions_reachable() {
        const page = makeVisibleSendPage()
        const review = findChild(page, "sendPaymentRequestReviewPopup")
        verify(review !== null)

        let longMessage = ""
        for (let i = 0; i < 120; ++i) {
            longMessage += "a message long enough to overflow the dialog "
        }
        page.applyPaymentRequest(
            "bitcoin:bcrt1qreviewaddress?message=" + encodeURIComponent(longMessage),
            "command line")
        tryCompare(review, "opened", true)

        verify(review.height <= page.height)
        const applyButton = findChild(review, "paymentRequestReviewApplyButton")
        verify(applyButton !== null)
        const bottom = applyButton.mapToItem(review.contentItem, 0, applyButton.height).y
        verify(bottom <= review.contentItem.height)

        applyButton.clicked()
        tryCompare(review, "opened", false)
        compare(testSendRecipient.address.address, "bcrt1qreviewaddress")
    }

    function test_wallet_change_during_review_reports_the_request_as_interrupted() {
        const page = makeVisibleSendPage()
        const review = findChild(page, "sendPaymentRequestReviewPopup")
        verify(review !== null)

        const outcomeSpy = createTemporaryObject(outcomeSpyComponent, testCase, {
            "target": page,
            "signalName": "paymentRequestOutcome"
        })
        verify(outcomeSpy !== null)

        page.applyPaymentRequest("bitcoin:bcrt1qreviewaddress?label=Alice", "command line")
        tryCompare(review, "opened", true)

        walletController.setSelectedWallet("wallet-change-during-review")
        tryCompare(review, "opened", false)

        tryVerify(function() { return outcomeSpy.count > 0 })
        compare(outcomeSpy.signalArguments[outcomeSpy.count - 1][0], "interrupted")
        compare(testSendRecipient.address.address, "bcrt1qsendtoaddress")
    }

    function test_available_balance_keeps_caption_static() {
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

        walletController.setSelectedWallet("review-only-wallet-change")

        tryCompare(popup, "opened", false)
        tryCompare(testWalletModel, "discardCurrentTransactionCalls", discardCallsBefore + 1)
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
        compare(reviewTitle.text, "Review Transaction")
        compare(closeButton.size, 45)
        compare(closeButton.iconSize, 15)
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
        const second = findChild(popup.contentItem, "sendTransactionReviewRecipient1")
        verify(second !== null)
        compare(findChild(second, "sendTransactionReviewRecipient1Title").text, "recipient-2")
        compare(findChild(popup.contentItem, "sendTransactionReviewRecipient0").showDivider, true)
        compare(second.showDivider, false)
        compare(findChild(second, "sendTransactionReviewRecipient1RequestBadge").visible, true)
        popup.close()
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
        page.manualCoinSelection = true
        testCoinsListModel.toggleCoinSelection(0)
        findChild(reviewStack, "sendResultDoneButton").clicked()
        tryCompare(popup, "opened", false)
        compare(page.manualCoinSelection, false)
        compare(testCoinsListModel.selectedCoinsCount, 0)
    }

    function test_custom_fee_send_uses_generic_confirmation_text() {
        const page = createTemporaryObject(sendComponent, testCase.Window.window.contentItem)
        verify(page !== null)
        page.width = testCase.width
        page.height = testCase.height
        page.visible = true
        testWalletModel.customFeeEnabled = true
        testWalletModel.customFeeRate = "4.2"
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
                "Your transaction was broadcast. Confirmation depends on its fee rate.")
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
        const page = createTemporaryObject(sendComponent, this)
        verify(page !== null)

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
        compare(page.prepareTransactionErrorText, "Amount plus fee exceeds available balance. Some of your coins are locked.")
        compare(prepareErrorText.text, "Amount plus fee exceeds available balance. Some of your coins are locked.")

        page.prepareTransactionErrorText = ""
        testCoinsListModel.toggleCoinSelection(0)
        continueButton.clicked()
        compare(testWalletModel.prepareTransactionCalls, callsBefore + 2)
        compare(page.prepareTransactionErrorText, "Selected inputs do not cover the amount plus fee")
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

        amountInput.text = ""
        amountInput.forceActiveFocus()
        verify(amountInput.activeFocus)
        const callsBefore = testWalletModel.scheduleFeeEstimatesCalls
        keyClick("1")

        tryVerify(function() { return testWalletModel.scheduleFeeEstimatesCalls > callsBefore })
        compare(amountInput.text, "1")
        compare(testSendRecipient.amount.display, "1.00000000")
    }

    function test_send_amount_allows_editing_whole_part_before_decimal() {
        const page = createTemporaryObject(sendComponent, this)
        verify(page !== null)

        const amountInput = findChild(page, "sendAmountInput")
        verify(amountInput !== null)

        amountInput.text = "0.00000000"
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

        compare(amountInput.text, "0.12345678")
        compare(testSendRecipient.amount.display, "0.12345678")

        amountInput.focus = false
        wait(0)
        compare(amountInput.activeFocus, false)
        compare(amountInput.text, "0.12345678")
    }

    function test_send_amount_rejects_extra_decimal_points_while_editing() {
        const page = createTemporaryObject(sendComponent, this)
        verify(page !== null)

        const amountInput = findChild(page, "sendAmountInput")
        verify(amountInput !== null)

        testSendRecipient.amount.display = "1.20000000"
        amountInput.text = "1.2"
        amountInput.cursorPosition = amountInput.text.length
        amountInput.forceActiveFocus()
        verify(amountInput.activeFocus)
        keyClick(".")

        compare(amountInput.text, "1.2")
        compare(testSendRecipient.amount.display, "1.20000000")
    }

    function test_send_uri_import_schedules_fee_estimate() {
        const page = createTemporaryObject(sendComponent, this)
        verify(page !== null)

        const sendPage = findChild(page, "walletSendPage")
        verify(sendPage !== null)

        const callsBefore = testWalletModel.scheduleFeeEstimatesCalls
        sendPage.applyPaymentRequestFromText(
            "bitcoin:bcrt1qdavt4j2sd7dlhqsavtnfxvzppw6k7qy97tmnu9?amount=0.02000000&label=uri-label",
            "clipboard"
        )

        // The mocked amount input still emits one schedule request when the
        // imported amount updates the bound field. The explicit URI-import
        // Refreshing the send form should contribute one more call.
        tryVerify(function() { return testWalletModel.scheduleFeeEstimatesCalls > callsBefore })
        compare(testSendRecipient.address.address, "bcrt1qdavt4j2sd7dlhqsavtnfxvzppw6k7qy97tmnu9")
        compare(testSendRecipient.amount.display, "0.02000000")
        compare(testSendRecipient.paymentRequestLabel, "uri-label")
        compare(testSendRecipient.label, "")
        compare(sendPage.paymentRequestStatus, "Payment request imported from clipboard")
        compare(sendPage.paymentRequestIsError, false)
    }

    function test_send_payment_uri_paste_applies_from_each_recipient_field_data() {
        return [
            { tag: "address", inputObjectName: "sendAddressInput" },
            { tag: "label", inputObjectName: "sendNoteInput" },
            { tag: "amount", inputObjectName: "sendAmountInput" },
        ]
    }

    function test_plain_address_paste_into_send_field() {
        const page = createTemporaryObject(sendComponent, this)
        verify(page !== null)

        const input = findChild(page, "sendAddressInput")
        verify(input !== null)
        const address = "bcrt1q8rcpp66mqmtsw27mx7awa7yfdrpzytumsapkw4"
        testSendRecipient.address.setAddress("")
        Clipboard.currentText = address
        input.forceActiveFocus()
        verify(input.activeFocus)
        pasteShortcut()

        compare(testSendRecipient.address.address, address)

        const replacement = "bcrt1qreplacementaddress000000000000000000000"
        Clipboard.currentText = replacement
        input.selectAll()
        pasteShortcut()
        compare(testSendRecipient.address.address, replacement)
    }

    function test_send_payment_uri_paste_applies_from_each_recipient_field(data) {
        const page = createTemporaryObject(sendComponent, this)
        verify(page !== null)

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

        compare(overwritePopup.opened, false)
        compare(testSendRecipient.address.address, address)
        compare(testSendRecipient.amount.satoshi, 10000000000)
        compare(testSendRecipient.paymentRequestLabel, "Alice")
        compare(testSendRecipient.label, "")
        compare(sendPage.paymentRequestMessage, "Pay me")
        compare(sendPage.paymentRequestStatus, "Payment request imported from clipboard")
        compare(sendPage.paymentRequestIsError, false)
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
        sendPage.handleClipboardPaste("address")
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

        sendPage.handleClipboardPaste("address")
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
        const page = createTemporaryObject(sendComponent, this)
        verify(page !== null)

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

        compare(testSendRecipient.address.address, address)
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

        sendPage.handlePaymentUriPaste("bitcoin:bcrt1qreplacement", "address")

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
        testWalletModel.customFeeRate = "2.5"
        compare(review.enabled, true)
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
    }

    function test_locked_coin_confirmation_uses_identity_after_refresh() {
        const page = createTemporaryObject(sendComponent, testCase.Window.window.contentItem, {width: 1000, height: 900})
        const popup = findChild(page, "coinSelectionPopup")
        popup.open()
        tryCompare(popup, "opened", true)
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
}

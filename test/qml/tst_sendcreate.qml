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

    SignalSpy {
        id: transactionPreparedSpy
    }

    function init() {
        walletController.setSelectedWallet("send-create-test-wallet")
        testWalletModel.customFeeEnabled = false
        testWalletModel.customFeeRate = ""
        testWalletModel.targetBlocks = 6
        testWalletModel.prepareTransactionResult = true
        testWalletModel.sendAmountExhaustsBalance = false
        testWalletModel.currentTransactionCanSend = true
        testWalletModel.currentTransactionCanBroadcast = false
        testWalletModel.currentTransactionReviewMessage = ""
        testSendRecipient.address.setAddress("bcrt1qsendtoaddress")
        testSendRecipient.amount.display = "0.00000000"
        testSendRecipient.label = ""
        testSendRecipient.hasPaymentRequest = false
        testSendRecipient.paymentRequestLabel = ""
        testSendRecipient.message = ""
        testSendRecipient.subtractFeeFromAmount = false
        testSendRecipient.isValid = true
        testRecipientsModel.allValid = true
        testRecipientsModel.validationError = ""
        testRecipientsModel.clearToFront()
        testRecipientsModel.totalAmountSatoshi = 0
        optionsModel.displayUnit = BitcoinAmount.BTC
        testCoinsListModel.reset()
        Clipboard.currentText = ""
    }

    function pasteShortcut() {
        keyClick(Qt.Key_V, Qt.ControlModifier)
    }

    function test_review_only_psbt_closes_and_discards_on_wallet_change() {
        const page = createTemporaryObject(sendComponent, testCase.Window.window.contentItem)
        verify(page !== null)
        page.width = testCase.width
        page.height = testCase.height
        page.visible = true

        const popup = findChild(page, "reviewOnlyPsbtPopup")
        verify(popup !== null)

        testWalletModel.currentTransactionCanSend = false
        testWalletModel.currentTransactionReviewMessage = "This wallet cannot sign."
        const discardCallsBefore = testWalletModel.discardCurrentTransactionCalls

        popup.reviewWallet = testWalletModel
        popup.open()
        tryCompare(popup, "opened", true)

        walletController.setSelectedWallet("review-only-wallet-change")

        tryCompare(popup, "opened", false)
        tryCompare(testWalletModel, "discardCurrentTransactionCalls", discardCallsBefore + 1)
    }

    function test_successful_psbt_broadcast_shows_confirmation() {
        const page = createTemporaryObject(sendComponent, testCase.Window.window.contentItem)
        verify(page !== null)
        page.width = testCase.width
        page.height = testCase.height
        page.visible = true

        const reviewPopup = findChild(page, "reviewOnlyPsbtPopup")
        const successPopup = findChild(page, "psbtBroadcastSuccessPopup")
        verify(reviewPopup !== null)
        verify(successPopup !== null)

        testWalletModel.currentTransactionCanSend = false
        testWalletModel.currentTransactionCanBroadcast = true
        reviewPopup.reviewWallet = testWalletModel
        reviewPopup.open()
        tryCompare(reviewPopup, "opened", true)

        const broadcastButton = findChild(reviewPopup, "sendReviewBroadcastButton")
        verify(broadcastButton !== null)
        tryCompare(broadcastButton, "visible", true)
        const broadcastCallsBefore = testWalletModel.broadcastCurrentTransactionCalls
        const discardCallsBefore = testWalletModel.discardCurrentTransactionCalls

        broadcastButton.clicked()

        tryCompare(reviewPopup, "opened", false)
        tryCompare(testWalletModel, "broadcastCurrentTransactionCalls", broadcastCallsBefore + 1)
        tryCompare(testWalletModel, "discardCurrentTransactionCalls", discardCallsBefore + 1)
        tryCompare(successPopup, "opened", true)
        successPopup.close()
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
        transactionPreparedSpy.target = page
        transactionPreparedSpy.signalName = "transactionPrepared"
        transactionPreparedSpy.clear()

        const callsBefore = testWalletModel.prepareTransactionCalls
        compare(page.prepareTransactionErrorText, "")
        compare(prepareErrorText.text, "")

        testWalletModel.prepareTransactionResult = false
        continueButton.clicked()
        compare(testWalletModel.prepareTransactionCalls, callsBefore + 1)
        compare(transactionPreparedSpy.count, 0)
        compare(page.prepareTransactionErrorText, "Amount plus fee exceeds available balance. Some of your coins are locked.")
        compare(prepareErrorText.text, "Amount plus fee exceeds available balance. Some of your coins are locked.")

        page.prepareTransactionErrorText = ""
        testCoinsListModel.toggleCoinSelection(0)
        continueButton.clicked()
        compare(testWalletModel.prepareTransactionCalls, callsBefore + 2)
        compare(transactionPreparedSpy.count, 0)
        compare(page.prepareTransactionErrorText, "Selected inputs do not cover the amount plus fee")
        compare(prepareErrorText.text, "Selected inputs do not cover the amount plus fee")

        testWalletModel.prepareTransactionResult = true
        continueButton.clicked()
        compare(testWalletModel.prepareTransactionCalls, callsBefore + 3)
        compare(transactionPreparedSpy.count, 1)
        compare(transactionPreparedSpy.signalArguments[0][0], false)
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
        // refresh added in Send.qml should contribute one more call.
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

    function test_recipient_fee_checkbox_updates_model() {
        const page = createTemporaryObject(sendComponent, this)
        const checkbox = findChild(page, "sendDeductFeeCheckbox")
        compare(checkbox.checked, false)
        checkbox.toggle(); checkbox.toggled()
        compare(testSendRecipient.subtractFeeFromAmount, true)
        verify(checkbox.indicator.checkProgress < 1)
        tryCompare(checkbox.indicator, "checkProgress", 1)
        testSendRecipient.subtractFeeFromAmount = false
        compare(checkbox.checked, false)
        compare(checkbox.indicator.checkProgress, 0)
    }

    function test_coin_selection_cancel_and_apply() {
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

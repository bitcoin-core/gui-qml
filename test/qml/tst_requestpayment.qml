// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtTest 1.2
import org.bitcoincore.qt 1.0
import "../../qml/controls/utils.js" as Utils
import "../../qml/pages/wallet"
import "../../qml/components"

TestCase {
    name: "RequestPayment"
    when: windowShown
    visible: true
    width: 900
    height: 700

    Component {
        id: requestPaymentComponent

        ReceivePage {}
    }

    function init() {
        optionsModel.displayUnit = BitcoinAmount.BTC
        testPaymentRequest.clear()
        otherPaymentRequest.clear()
        testWalletModel.lastCommitAddressType = ""
        testWalletModel.lastTemplateRequestId = ""
        testWalletModel.lastRemovedRequestId = ""
        testWalletModel.removeReceiveRequestResult = true
        testWalletModel.receiveRequestReconciliationPending = false
        walletController.closePaymentRequestDetailRequests = 0
    }

    function test_receiving_address_reconnects_after_wallet_selection_and_gc() {
        const page = createTemporaryObject(requestPaymentComponent, this, { wallet: null })
        verify(page)
        const card = findChild(page, "receivingAddressCard")
        const menu = findChild(card, "receivingAddressQRContextMenu")
        const request = testWalletModel.receivingAddress
        request.clear()

        for (let selection = 0; selection < 2; ++selection) {
            page.wallet = null
            gc()
            page.wallet = testWalletModel
            compare(card.request, request)
            verify(testWalletModel.ensureReceivingAddress())
            menu.open()
            tryCompare(menu, "opened", true)
            request.addressChanged()
            tryCompare(menu, "opened", false)
        }

        page.wallet = null
        request.clear()
    }

    function test_formatRelativeTime_empty() {
        compare(Utils.formatRelativeTime(""), "")
        compare(Utils.formatRelativeTime(null), "")
        compare(Utils.formatRelativeTime(undefined), "")
    }

    function test_formatRelativeTime_just_now() {
        var now = new Date()
        compare(Utils.formatRelativeTime(now.toISOString()), "just now")
    }

    function test_formatRelativeTime_minutes() {
        var d = new Date()
        d.setMinutes(d.getMinutes() - 5)
        compare(Utils.formatRelativeTime(d.toISOString()), "5 minutes ago")

        var d1 = new Date()
        d1.setMinutes(d1.getMinutes() - 1)
        compare(Utils.formatRelativeTime(d1.toISOString()), "1 minute ago")
    }

    function test_formatRelativeTime_hours() {
        var d = new Date()
        d.setHours(d.getHours() - 3)
        compare(Utils.formatRelativeTime(d.toISOString()), "3 hours ago")

        var d1 = new Date()
        d1.setHours(d1.getHours() - 1)
        compare(Utils.formatRelativeTime(d1.toISOString()), "1 hour ago")
    }

    function test_formatRelativeTime_days() {
        var d = new Date()
        d.setDate(d.getDate() - 2)
        compare(Utils.formatRelativeTime(d.toISOString()), "2 days ago")

        var d1 = new Date()
        d1.setDate(d1.getDate() - 1)
        compare(Utils.formatRelativeTime(d1.toISOString()), "1 day ago")
    }

    Component {
        id: btcValidatorComponent
        TextField {
            validator: RegularExpressionValidator {
                regularExpression: /^0*\d{0,8}(\.\d{0,8})?$/
            }
            maximumLength: 32
        }
    }

    Component {
        id: satValidatorComponent
        TextField {
            validator: RegularExpressionValidator {
                regularExpression: /^0*\d{0,16}$/
            }
            maximumLength: 32
        }
    }

    function test_btcValidator_accepts_valid_amounts() {
        var field = createTemporaryObject(btcValidatorComponent, this)
        verify(field !== null)

        field.text = "0"
        compare(field.acceptableInput, true)

        field.text = "1.00000000"
        compare(field.acceptableInput, true)

        field.text = "21000000.00000000"
        compare(field.acceptableInput, true)

        field.text = "0.00000001"
        compare(field.acceptableInput, true)

        field.text = "01.00000000"
        compare(field.acceptableInput, true)

        field.text = "00000001.00000000"
        compare(field.acceptableInput, true)
    }

    function test_btcValidator_rejects_invalid_amounts() {
        var field = createTemporaryObject(btcValidatorComponent, this)
        verify(field !== null)

        field.text = "-1"
        compare(field.acceptableInput, false)

        field.text = "1.2.3"
        compare(field.acceptableInput, false)

        field.text = "0.000000001"
        compare(field.acceptableInput, false)

        field.text = "100000000.00000000"
        compare(field.acceptableInput, false)

        field.text = "abc"
        compare(field.acceptableInput, false)
    }

    function test_satValidator_accepts_valid_amounts() {
        var field = createTemporaryObject(satValidatorComponent, this)
        verify(field !== null)

        field.text = "0"
        compare(field.acceptableInput, true)

        field.text = "100000000"
        compare(field.acceptableInput, true)

        field.text = "2100000000000000"
        compare(field.acceptableInput, true)

        field.text = "00"
        compare(field.acceptableInput, true)

        field.text = "0002100000000000000"
        compare(field.acceptableInput, true)
    }

    function test_satValidator_rejects_invalid_amounts() {
        var field = createTemporaryObject(satValidatorComponent, this)
        verify(field !== null)

        field.text = "1.5"
        compare(field.acceptableInput, false)

        field.text = "-100"
        compare(field.acceptableInput, false)

        field.text = "10000000000000000"
        compare(field.acceptableInput, false)
    }


    function createPage() {
        const page = createTemporaryObject(requestPaymentComponent, this, { width: 900, height: 900, wallet: testWalletModel })
        verify(page !== null)
        tryVerify(function() { return testWalletModel.receivingAddress.address !== "" })
        tryCompare(testWalletModel, "receiveOperationPending", false)
        return page
    }
    function createRequest(page) {
        const card = page.draftCard
        if (!card.hasFields) editField(card, "requestPaymentYourNameInput", "Request")
        card.createRequest()
        tryCompare(page.requestModal, "opened", true)
        return page.requestModal.card
    }

    Component {
        id: receivingCardComponent
        ReceivingAddressCard {}
    }

    Component {
        id: paymentCardComponent
        PaymentRequestCard {}
    }

    PaymentRequest { id: otherPaymentRequest }

    QtObject {
        id: otherReceiveWallet
        readonly property var receivingAddress: otherPaymentRequest
        readonly property var receiveAddressTypes: testWalletModel.receiveAddressTypes
        property bool receiveOperationPending: false
        property bool receiveRequestReconciliationPending: false
        property string receiveOperationError: ""
        function receiveAddressTypeLabel(type) { return testWalletModel.receiveAddressTypeLabel(type) }
        signal receiveOperationFinished(string operation, bool success)
    }

    function test_receiving_completion_survives_collection_and_wallet_changes() {
        const card = createTemporaryObject(receivingCardComponent, this, { wallet: testWalletModel })
        verify(card !== null)
        for (let i = 0; i < 3; ++i) {
            gc()
            card.wallet = otherReceiveWallet
            compare(card.request, otherPaymentRequest)
            gc()
            otherReceiveWallet.receiveOperationError = "New wallet " + i
            otherReceiveWallet.receiveOperationFinished("address", false)
            compare(card.errorText, "New wallet " + i)
            testWalletModel.receiveOperationFinished("address", false)
            compare(card.errorText, "New wallet " + i)
            card.wallet = testWalletModel
            gc()
            testWalletModel.receiveOperationError = "Original wallet " + i
            testWalletModel.receiveOperationFinished("address", false)
            compare(card.errorText, "Original wallet " + i)
            otherReceiveWallet.receiveOperationFinished("address", false)
            compare(card.errorText, "Original wallet " + i)
        }
        testWalletModel.receiveOperationError = ""
    }

    SignalSpy {
        id: receiveMenuClosing
        signalName: "aboutToHide"
    }

    function test_receiving_request_changes_survive_collection() {
        const card = createTemporaryObject(receivingCardComponent, this, { wallet: testWalletModel, width: 480 })
        verify(card !== null)
        const menu = findChild(card, "receivingAddressQRContextMenu")
        receiveMenuClosing.target = menu
        for (let i = 0; i < 2; ++i) {
            const previous = card.request
            gc()
            card.wallet = i === 0 ? otherReceiveWallet : testWalletModel
            gc()
            menu.open()
            tryCompare(menu, "opened", true)
            receiveMenuClosing.clear()
            previous.addressChanged()
            compare(receiveMenuClosing.count, 0)
            card.request.addressChanged()
            compare(receiveMenuClosing.count, 1)
            tryCompare(menu, "visible", false)
        }
        receiveMenuClosing.target = null
    }

    function test_payment_request_handlers_survive_collection_and_target_changes() {
        const card = createTemporaryObject(paymentCardComponent, this, {
            wallet: testWalletModel, request: testPaymentRequest, width: 532
        })
        verify(card !== null)
        for (let i = 0; i < 2; ++i) {
            const previousWallet = card.wallet
            const previousRequest = card.request
            gc()
            card.wallet = i === 0 ? otherReceiveWallet : testWalletModel
            card.request = i === 0 ? otherPaymentRequest : testPaymentRequest
            gc()
            card.pendingOperation = "save"
            card.wallet.receiveOperationError = "Save failed " + i
            previousWallet.receiveOperationFinished("save", false)
            compare(card.pendingOperation, "save")
            card.wallet.receiveOperationFinished("save", false)
            compare(card.pendingOperation, "")
            compare(card.errorText, "Save failed " + i)
            compare(card.request.id, "")
            compare(card.saved, false)
            previousRequest.idChanged()
            compare(card.errorText, "Save failed " + i)
            card.request.idChanged()
            compare(card.errorText, "")
        }
        testWalletModel.receiveOperationError = ""
    }

    function test_address_ready_before_request_and_create_requires_fields() {
        const page = createPage()
        const address = testWalletModel.receivingAddress.address
        compare(testPaymentRequest.id, "")
        verify(findChild(page, "receivingAddressQRImage").visible)
        compare(findChild(page, "receivingAddressQRImage").code, address)
        const button = findChild(page.draftCard, "requestPaymentGenerateButton")
        verify(!button.enabled)
        page.draftCard.createRequest()
        compare(testPaymentRequest.id, "")
        editField(page.draftCard, "requestPaymentNoteSelfInput", "Private note")
        verify(button.enabled)
        const card = createRequest(page)
        compare(card.saved, true)
        compare(findChild(card, "paymentRequestStatus").text, "Awaiting payment")
        page.requestModal.close()
        tryCompare(page.requestModal, "visible", false)
        verify(testWalletModel.receivingAddress.address !== "")
        verify(testWalletModel.receivingAddress.address !== address)
        compare(testPaymentRequest.id, "")
    }

    function test_next_address_and_type_selection_rotate_without_request() {
        const page = createPage()
        const receiving = findChild(page, "receivingAddressCard")
        const first = testWalletModel.receivingAddress.address
        receiving.ensureAddress(true, "")
        tryCompare(testWalletModel, "receiveOperationPending", false)
        verify(testWalletModel.receivingAddress.address !== first)
        receiving.ensureAddress(false, "p2sh-segwit")
        tryCompare(testWalletModel, "receiveOperationPending", false)
        compare(testWalletModel.receivingAddress.addressType, "p2sh-segwit")
        compare(testPaymentRequest.id, "")
        const card = createRequest(page)
        compare(testWalletModel.lastCommitAddressType, "p2sh-segwit")
    }

    function test_qr_context_menus_close_when_address_is_no_longer_shareable() {
        const page = createPage()
        const receivingArea = findChild(page, "receivingAddressQRContextArea")
        const receivingMenu = findChild(page, "receivingAddressQRContextMenu")
        mouseClick(receivingArea, 20, 20, Qt.RightButton)
        tryCompare(receivingMenu, "opened", true)
        compare(findChild(page, "receivingAddressQRContextCopy").text, "Copy QR code")
        compare(findChild(page, "receivingAddressQRContextSave").text, "Save QR code")
        findChild(page, "receivingAddressCard").ensureAddress(true, "")
        tryCompare(receivingMenu, "visible", false)

        const card = createRequest(page)
        const requestMenu = findChild(card, "requestPaymentQRContextMenu")
        mouseClick(findChild(card, "requestPaymentQRContextArea"), 20, 20, Qt.RightButton)
        tryCompare(requestMenu, "opened", true)
        compare(findChild(card, "requestPaymentQRContextCopy").text, "Copy QR code")
        compare(findChild(card, "requestPaymentQRContextSave").text, "Save QR code")
        card.request.paymentReceived = true
        tryCompare(requestMenu, "visible", false)
        verify(!findChild(card, "requestPaymentQRContextArea").enabled)
    }

    function test_primary_action_returns_to_copy_after_update() {
        const page = createPage()
        const card = createRequest(page)
        const copy = findChild(card, "requestPaymentCopyButton")
        const update = findChild(card, "requestPaymentUpdateButton")
        verify(copy.visible)
        verify(!update.visible)
        editField(card, "requestPaymentYourNameInput", "Hal")
        verify(!copy.visible)
        verify(update.visible)
        update.clicked()
        tryCompare(testWalletModel, "receiveOperationPending", false)
        compare(card.request.label, "Hal")
        verify(page.requestModal.opened)
        verify(copy.visible)
        verify(!update.visible)
        card.request.paymentReceived = true
        verify(!copy.visible)
        editField(card, "requestPaymentNoteSelfInput", "Received")
        verify(update.visible)
        update.clicked()
        tryCompare(testWalletModel, "receiveOperationPending", false)
        verify(!copy.visible)
        verify(!update.visible)
    }

    function test_saved_request_unit_toggle_does_not_enable_update() {
        const page = createPage()
        editField(page.draftCard, "requestPaymentAmountInput", "0.001")
        const card = createRequest(page)
        const amount = card.request.amount.satoshi
        const button = findChild(card, "requestPaymentUpdateButton")
        const icon = findChild(card, "requestPaymentAmountUnitIcon")
        tryCompare(icon, "width", 12)
        tryCompare(icon, "height", 12)
        verify(!button.enabled)
        card.toggleAmountUnit()
        compare(optionsModel.displayUnit, BitcoinAmount.SAT)
        compare(card.request.amount.satoshi, amount)
        verify(!button.enabled)
        tryCompare(icon, "width", 12)
        tryCompare(icon, "height", 12)
        card.toggleAmountUnit()
        compare(optionsModel.displayUnit, BitcoinAmount.BTC)
        compare(card.request.amount.satoshi, amount)
        verify(!button.enabled)
    }

    function test_update_requires_at_least_one_detail() {
        const card = createRequest(createPage())
        editField(card, "requestPaymentAmountInput", "")
        editField(card, "requestPaymentMessageInput", "")
        editField(card, "requestPaymentNoteSelfInput", "")
        editField(card, "requestPaymentYourNameInput", "   ")
        verify(!findChild(card, "requestPaymentUpdateButton").enabled)
        verify(!card.saveFields())
        compare(card.request.label, "Request")
        editField(card, "requestPaymentNoteSelfInput", "Keep privately")
        verify(findChild(card, "requestPaymentUpdateButton").enabled)
        verify(card.saveFields())
        tryCompare(testWalletModel, "receiveOperationPending", false)
        compare(card.request.noteSelf, "Keep privately")
    }

    function test_request_again_prefills_without_saving_data() {
        return [{tag: "unpaid", paid: false}, {tag: "paid", paid: true}]
    }
    function test_request_again_prefills_without_saving(data) {
        const page = createPage()
        editField(page.draftCard, "requestPaymentNoteSelfInput", "Lunch with Hal")
        const card = createRequest(page)
        const id = card.request.id
        card.request.paymentReceived = data.paid
        const opened = walletController.openReceiveRequests
        findChild(card, "paymentRequestMoreButton").clicked()
        tryCompare(findChild(card, "paymentRequestMoreMenu"), "opened", true)
        findChild(card, "requestPaymentAgainMenuButton").clicked()
        tryCompare(page.requestModal, "visible", false)
        tryCompare(walletController, "openReceiveRequests", opened + 1)
        compare(testWalletModel.lastTemplateRequestId, id)
        compare(testPaymentRequest.id, "")
        compare(testPaymentRequest.noteSelf, "Lunch with Hal")
        tryCompare(findChild(page.draftCard, "requestPaymentNoteSelfInput"), "text", "Lunch with Hal")
        tryCompare(findChild(page.draftCard, "requestPaymentGenerateButton"), "enabled", true)
    }

    function editField(card, objectName, text) {
        const input = findChild(card, objectName)
        input.forceActiveFocus()
        input.text = text
        input.textEdited()
        return input
    }

    function test_fields_save_explicitly_without_changing_address() {
        const card = createRequest(createPage())
        const address = card.request.address
        const input = editField(card, "requestPaymentYourNameInput", "Friday coffee")
        findChild(card, "requestPaymentMessageInput").forceActiveFocus()
        compare(card.request.label, "Request")
        verify(card.saveFields())
        tryCompare(testWalletModel, "receiveOperationPending", false)
        compare(card.request.label, "Friday coffee")
        compare(card.request.address, address)
        verify(card.sharing)
        verify(findChild(card, "requestPaymentQRImage").visible)
        editField(card, "requestPaymentYourNameInput", "")
        findChild(card, "requestPaymentMessageInput").forceActiveFocus()
        verify(!card.saveFields())
        compare(card.request.label, "Friday coffee")
    }

    function test_closing_modal_discards_unsaved_field() {
        const page = createPage()
        const card = createRequest(page)
        editField(card, "requestPaymentMessageInput", "Saved on close")
        const request = card.request
        page.requestModal.close()
        tryCompare(page.requestModal, "visible", false)
        compare(request.message, "")
    }

    function test_received_payment_freezes_fields_and_hides_sharing() {
        const card = createRequest(createPage())
        const message = editField(card, "requestPaymentMessageInput", "Uncommitted message")
        card.request.receivedAmountSatoshi = 141
        card.request.paymentReceived = true
        compare(card.request.message, "")
        compare(message.text, "")
        verify(!card.sharing)
        verify(!findChild(card, "requestPaymentQRPlaceholder").visible)
        const summary = findChild(card, "requestPaymentReceivedSummary")
        verify(summary.visible)
        compare(summary.amountValue, "0.00000141")
        verify(!findChild(card, "requestPaymentReceivedIcon").dashed)
        findChild(card, "paymentRequestMoreButton").clicked()
        const menu = findChild(card, "paymentRequestMoreMenu")
        tryCompare(menu, "opened", true)
        verify(!findChild(card, "requestPaymentCopyQRMenuButton").visible)
        verify(!findChild(card, "requestPaymentSaveQRMenuButton").visible)
        verify(findChild(card, "requestPaymentDeleteMenuButton").visible)
        menu.close()
        verify(!findChild(card, "requestPaymentAddressText").interactive)
        verify(!findChild(card, "requestPaymentAmountInput").enabled)
        verify(!findChild(card, "requestPaymentYourNameInput").enabled)
        verify(!message.enabled)
        const note = editField(card, "requestPaymentNoteSelfInput", "Received, thank you")
        note.editingFinished()
        verify(card.saveFields())
        tryCompare(testWalletModel, "receiveOperationPending", false)
        compare(card.request.noteSelf, "Received, thank you")
    }

    function test_saved_request_cannot_be_shared_while_payment_scan_is_pending() {
        const page = createPage()
        const saved = createRequest(page)
        const requestId = saved.request.id
        page.requestModal.close()
        tryCompare(page.requestModal, "visible", false)
        testWalletModel.receiveRequestReconciliationPending = true
        verify(page.requestModal.openRequest(requestId))
        tryCompare(page.requestModal, "opened", true)
        const card = page.requestModal.card
        verify(!card.sharing)
        verify(!findChild(card, "requestPaymentQRImage").visible)
        verify(!findChild(card, "requestPaymentCopyButton").enabled)
        verify(!findChild(card, "requestPaymentAddressText").interactive)
        card.copyRequest()

        // The saved unpaid flag can be stale until the scan marks a payment.
        card.request.paymentReceived = true
        testWalletModel.receiveRequestReconciliationPending = false
        verify(!card.sharing)
        verify(!findChild(card, "requestPaymentQRImage").visible)
        verify(!findChild(card, "requestPaymentCopyButton").visible)
    }

    function test_payment_arrival_preserves_private_note_draft() {
        const card = createRequest(createPage())
        const note = editField(card, "requestPaymentNoteSelfInput", "Keep this draft")
        card.request.paymentReceived = true
        compare(note.text, "Keep this draft")
        verify(note.enabled)
        note.editingFinished()
        verify(card.saveFields())
        tryCompare(testWalletModel, "receiveOperationPending", false)
        compare(card.request.noteSelf, "Keep this draft")
    }

    function test_amount_input_rejects_invalid_characters_and_preserves_precision() {
        const card = createRequest(createPage())
        optionsModel.displayUnit = BitcoinAmount.BTC
        const input = editField(card, "requestPaymentAmountInput", "0.12345678")
        input.cursorPosition = input.text.length
        keyClick("9")
        compare(input.text, "0.12345678")
        keyClick(".")
        keyClick("x")
        compare(input.text, "0.12345678")
        input.editingFinished()
        verify(card.saveFields())
        tryCompare(testWalletModel, "receiveOperationPending", false)
        compare(card.request.amount.satoshi, 12345678)
        compare(input.text, "0.12345678")
    }

    function test_unit_toggle_updates_app_without_converting_edited_text() {
        const page = createPage()
        const card = createRequest(page)
        optionsModel.displayUnit = BitcoinAmount.BTC
        const input = editField(card, "requestPaymentAmountInput", "12")
        findChild(card, "requestPaymentAmountUnitToggle").clicked()
        compare(card.amountUnit, BitcoinAmount.SAT)
        compare(optionsModel.displayUnit, BitcoinAmount.SAT)
        compare(page.draftCard.amountUnit, BitcoinAmount.SAT)
        compare(input.text, "12")
        verify(card.saveFields())
        tryCompare(testWalletModel, "receiveOperationPending", false)
        compare(card.request.amount.satoshi, 12)
        findChild(card, "requestPaymentAmountUnitToggle").clicked()
        compare(input.text, "0.00000012")
        verify(!findChild(card, "requestPaymentUpdateButton").enabled)
        compare(card.request.amount.satoshi, 12)
        compare(optionsModel.displayUnit, BitcoinAmount.BTC)
        compare(page.draftCard.amountUnit, BitcoinAmount.BTC)
        optionsModel.displayUnit = BitcoinAmount.SAT
        compare(card.amountUnit, BitcoinAmount.SAT)
        optionsModel.displayUnit = BitcoinAmount.BTC
    }

    function test_fractional_sats_and_out_of_range_amounts_never_save() {
        const page = createPage()
        const card = page.draftCard
        optionsModel.displayUnit = BitcoinAmount.SAT
        editField(card, "requestPaymentAmountInput", "1.5")
        card.createRequest()
        compare(card.request.id, "")
        verify(card.errorText.length > 0)
        optionsModel.displayUnit = BitcoinAmount.BTC
        editField(card, "requestPaymentAmountInput", "21000000.00000001")
        card.createRequest()
        compare(card.request.id, "")
        editField(card, "requestPaymentAmountInput", "21000000.00000000")
        verify(card.saveFields())
        tryCompare(testWalletModel, "receiveOperationPending", false)
        compare(card.request.amount.satoshi, 2100000000000000)
    }

    function test_delete_menu_closes_modal_and_clears_matching_request() {
        const page = createPage()
        const card = createRequest(page)
        const requestId = card.request.id
        findChild(card, "paymentRequestMoreButton").clicked()
        tryCompare(findChild(card, "paymentRequestMoreMenu"), "opened", true)
        verify(findChild(card, "requestPaymentCopyQRMenuButton").visible)
        verify(findChild(card, "requestPaymentSaveQRMenuButton").visible)
        findChild(card, "requestPaymentDeleteMenuButton").clicked()
        tryCompare(testWalletModel, "receiveOperationPending", false)
        tryCompare(page.requestModal, "visible", false)
        compare(testWalletModel.lastRemovedRequestId, requestId)
        compare(card.request.id, "")
        compare(testPaymentRequest.id, "")
        verify(page.draftCard.visible)
    }

    function test_delete_failure_preserves_request_and_modal() {
        const page = createPage()
        const card = createRequest(page)
        const requestId = card.request.id
        testWalletModel.removeReceiveRequestResult = false
        findChild(card, "paymentRequestMoreButton").clicked()
        tryCompare(findChild(card, "paymentRequestMoreMenu"), "opened", true)
        findChild(card, "requestPaymentDeleteMenuButton").clicked()
        tryCompare(testWalletModel, "receiveOperationPending", false)
        verify(page.requestModal.opened)
        compare(card.request.id, requestId)
        compare(testWalletModel.lastRemovedRequestId, "")
        verify(card.errorText.length > 0)
    }

    function test_history_button_requests_navigation() {
        const page = createPage()
        let requests = 0
        page.addressHistoryRequested.connect(function() { ++requests })
        findChild(page, "receiveMoreButton").clicked()
        tryCompare(findChild(page, "receiveMoreMenu"), "opened", true)
        findChild(page, "requestPaymentHistoryButton").clicked()
        tryCompare(findChild(page, "receiveMoreMenu"), "opened", false)
        compare(requests, 1)
    }
}

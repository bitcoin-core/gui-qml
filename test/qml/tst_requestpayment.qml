// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtTest 1.2
import org.bitcoincore.qt 1.0
import "../../qml/controls/utils.js" as Utils
import "../../qml/pages/wallet"

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
        testWalletModel.lastCommitAddressType = ""
        testWalletModel.lastTemplateRequestId = ""
        testWalletModel.lastRemovedRequestId = ""
        testWalletModel.removeReceiveRequestResult = true
        testWalletModel.receiveRequestReconciliationPending = false
        walletController.closePaymentRequestDetailRequests = 0
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
        return page
    }
    function createRequest(page) {
        const card = page.draftCard
        if (!card.hasFields) editField(card, "requestPaymentYourNameInput", "Request")
        card.createRequest()
        tryCompare(page.requestModal, "opened", true)
        return page.requestModal.card
    }

    function test_customize_fields_matches_visible_rows() {
        const page = createPage()
        const card = page.draftCard
        const amount = findChild(card, "requestPaymentAmountField")
        const recipient = findChild(card, "requestPaymentLabelRow")
        const message = findChild(card, "requestPaymentMessageRow")
        verify(amount.visible)
        verify(message.visible)
        verify(!recipient.visible)
        compare(message.label, "Message")
        const footer = findChild(card, "requestPaymentDetailsFooter")
        verify(footer.visible)

        waitForRendering(page)
        mouseClick(findChild(card, "requestPaymentCustomizeButton"))
        const menu = findChild(card, "requestPaymentFieldsMenu")
        tryCompare(menu, "opened", true)
        const picker = findChild(card, "requestPaymentFieldsPicker")
        tryVerify(function() { return picker.itemAtIndex(2) !== null })
        const amountOption = picker.itemAtIndex(0)
        const messageOption = picker.itemAtIndex(1)
        const recipientOption = picker.itemAtIndex(2)
        verify(amountOption.selected)
        verify(messageOption.selected)
        verify(!recipientOption.selected)
        compare(recipientOption.text, "Recipient Name")

        mouseClick(recipientOption)
        verify(recipient.visible)
        verify(recipientOption.selected)
        verify(menu.opened)
        mouseClick(messageOption)
        verify(!message.visible)
        verify(!messageOption.selected)
        verify(!recipient.showDivider)
        verify(amount.showDivider)
        mouseClick(amountOption)
        verify(!amount.visible)
        verify(!amountOption.selected)
        mouseClick(recipientOption)
        verify(!recipient.visible)
        verify(!footer.visible)
        verify(findChild(card, "requestPaymentGenerateButton").enabled)

        keyClick(Qt.Key_Escape)
        tryCompare(menu, "visible", false)
        mouseClick(findChild(card, "requestPaymentCustomizeButton"))
        tryCompare(menu, "opened", true)
        verify(!amountOption.selected)
        verify(!messageOption.selected)
        verify(!recipientOption.selected)
        menu.close()
        tryCompare(menu, "visible", false)
        card.createRequest()
        tryCompare(page.requestModal, "opened", true)
        verify(findChild(page.requestModal.card, "requestPaymentAmountField").visible)
        verify(findChild(page.requestModal.card, "requestPaymentLabelRow").visible)
        verify(findChild(page.requestModal.card, "requestPaymentMessageRow").visible)
        compare(findChild(page.requestModal.card, "requestPaymentCustomizeButton"), null)
    }

    function test_customize_preserves_field_values_when_hidden() {
        const page = createPage()
        const card = page.draftCard
        const input = editField(card, "requestPaymentMessageInput", "Lunch split")
        input.editingFinished()
        const address = card.request.address
        waitForRendering(page)
        mouseClick(findChild(card, "requestPaymentCustomizeButton"))
        const menu = findChild(card, "requestPaymentFieldsMenu")
        tryCompare(menu, "opened", true)
        const picker = findChild(card, "requestPaymentFieldsPicker")
        tryVerify(function() { return picker.itemAtIndex(1) !== null })
        const option = picker.itemAtIndex(1)
        mouseClick(option)
        verify(!findChild(card, "requestPaymentMessageRow").visible)
        compare(input.text, "Lunch split")
        compare(card.request.message, "Lunch split")
        mouseClick(option)
        verify(findChild(card, "requestPaymentMessageRow").visible)
        compare(input.text, "Lunch split")
        compare(card.request.address, address)
        menu.close()
    }

    function test_empty_request_can_be_created_without_showing_address() {
        const page = createPage()
        const address = testWalletModel.receivingAddress.address
        compare(testPaymentRequest.id, "")
        compare(findChild(page, "receivingAddressCard"), null)
        compare(findChild(page, "receivingAddressQRImage"), null)
        compare(findChild(page.draftCard, "requestPaymentAddressTypeSection"), null)
        const button = findChild(page.draftCard, "requestPaymentGenerateButton")
        verify(button.enabled)
        page.draftCard.createRequest()
        tryCompare(page.requestModal, "opened", true)
        const card = page.requestModal.card
        compare(card.saved, true)
        compare(card.request.address, address)
        compare(card.request.amount.satoshi, 0)
        compare(card.request.label, "")
        compare(card.request.message, "")
        compare(card.request.noteSelf, "")
        compare(findChild(card, "paymentRequestStatus").text, "Awaiting payment")
        compare(findChild(card, "requestPaymentAddressTypeSection"), null)
        page.requestModal.close()
        tryCompare(page.requestModal, "visible", false)
        verify(testWalletModel.receivingAddress.address !== "")
        verify(testWalletModel.receivingAddress.address !== address)
        compare(testPaymentRequest.id, "")
    }

    function test_next_address_and_type_selection_rotate_without_request() {
        const page = createPage()
        const first = testWalletModel.receivingAddress.address
        page.ensureReceivingAddress(true, "")
        verify(testWalletModel.receivingAddress.address !== first)
        const settingsButton = findChild(page, "receiveAddressSettingsButton")
        const moreButton = findChild(page, "receiveMoreButton")
        compare(settingsButton.width, moreButton.width)
        compare(settingsButton.height, moreButton.height)
        verify(settingsButton.x + settingsButton.width <= moreButton.x)
        compare(settingsButton.iconSource.toString(), "qrc:/icons/address-settings.svg")
        compare(findChild(findChild(page, "receiveMoreMenu"), "receiveAddressTypePicker"), null)
        mouseClick(settingsButton)
        const menu = findChild(page, "receiveAddressTypeMenu")
        tryCompare(menu, "opened", true)
        const picker = findChild(page, "receiveAddressTypePicker")
        compare(picker.title, "Address Type")
        compare(picker.model.length, 4)
        tryVerify(function() { return picker.itemAtIndex(3) !== null })
        for (let i = 0; i < 4; ++i) {
            compare(picker.itemAtIndex(i).selected, picker.itemAtIndex(i).rowValue === picker.currentValue)
            compare(picker.itemAtIndex(i).subtitle, picker.model[i].description)
            verify(picker.itemAtIndex(i).subtitle.length > 0)
        }
        mouseClick(picker.itemAtIndex(2))
        tryCompare(menu, "visible", false)
        compare(testWalletModel.receivingAddress.addressType, "p2sh-segwit")
        compare(picker.currentValue, "p2sh-segwit")
        verify(picker.itemAtIndex(2).selected)
        compare(testPaymentRequest.id, "")
        const card = createRequest(page)
        compare(testWalletModel.lastCommitAddressType, "p2sh-segwit")
    }

    function test_clear_form_removes_hidden_and_invalid_drafts_without_changing_address() {
        const page = createPage()
        const card = page.draftCard
        const address = testWalletModel.receivingAddress.address
        const type = testWalletModel.receivingAddress.addressType
        editField(card, "requestPaymentAmountInput", "0.0001").editingFinished()
        editField(card, "requestPaymentYourNameInput", "Alice").editingFinished()
        editField(card, "requestPaymentMessageInput", "Lunch split").editingFinished()
        editField(card, "requestPaymentNoteSelfInput", "Lunch with Hal").editingFinished()
        compare(card.request.amount.satoshi, 10000)
        compare(card.request.label, "Alice")
        editField(card, "requestPaymentAmountInput", ".").editingFinished()
        verify(card.errorText.length > 0)
        card.showRecipientName = false
        card.showAmount = false
        card.showMessage = false

        mouseClick(findChild(page, "receiveMoreButton"))
        const menu = findChild(page, "receiveMoreMenu")
        tryCompare(menu, "opened", true)
        const clear = findChild(page, "receiveClearFormButton")
        compare(clear.text, "Clear form")
        mouseClick(clear)
        tryCompare(menu, "visible", false)
        compare(card.request.amount.satoshi, 0)
        compare(card.request.label, "")
        compare(card.request.message, "")
        compare(card.request.noteSelf, "")
        compare(card.errorText, "")
        verify(!card.hasFields)
        verify(!card.modified)
        verify(!card.showRecipientName)
        verify(!card.showAmount)
        verify(!card.showMessage)
        compare(testWalletModel.receivingAddress.address, address)
        compare(testWalletModel.receivingAddress.addressType, type)
        card.createRequest()
        tryCompare(page.requestModal, "opened", true)
        compare(page.requestModal.card.request.address, address)
    }

    function test_qr_context_menus_close_when_address_is_no_longer_shareable() {
        const page = createPage()
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
        compare(card.request.label, "Hal")
        verify(page.requestModal.opened)
        verify(copy.visible)
        verify(!update.visible)
        card.request.paymentReceived = true
        verify(!copy.visible)
        editField(card, "requestPaymentNoteSelfInput", "Received")
        verify(update.visible)
        update.clicked()
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

    function test_request_again_reveals_populated_hidden_fields() {
        const page = createPage()
        editField(page.draftCard, "requestPaymentAmountInput", "0.001").editingFinished()
        editField(page.draftCard, "requestPaymentMessageInput", "Lunch split").editingFinished()
        editField(page.draftCard, "requestPaymentYourNameInput", "Hal").editingFinished()
        page.draftCard.createRequest()
        tryCompare(page.requestModal, "opened", true)
        const requestId = page.requestModal.card.request.id
        page.requestModal.close()
        tryCompare(page.requestModal, "visible", false)

        page.draftCard.showAmount = false
        page.draftCard.showMessage = false
        page.draftCard.showRecipientName = false
        verify(page.requestModal.openRequest(requestId))
        tryCompare(page.requestModal, "opened", true)
        const card = page.requestModal.card
        findChild(card, "paymentRequestMoreButton").clicked()
        tryCompare(findChild(card, "paymentRequestMoreMenu"), "opened", true)
        findChild(card, "requestPaymentAgainMenuButton").clicked()
        tryCompare(page.requestModal, "visible", false)
        tryCompare(findChild(page.draftCard, "requestPaymentGenerateButton"), "enabled", true)

        compare(testPaymentRequest.id, "")
        compare(findChild(page.draftCard, "requestPaymentAmountInput").text, "0.00100000")
        compare(findChild(page.draftCard, "requestPaymentMessageInput").text, "Lunch split")
        compare(findChild(page.draftCard, "requestPaymentYourNameInput").text, "Hal")
        verify(findChild(page.draftCard, "requestPaymentAmountField").visible)
        verify(findChild(page.draftCard, "requestPaymentMessageRow").visible)
        verify(findChild(page.draftCard, "requestPaymentLabelRow").visible)
    }

    function test_empty_request_reverted_edit_restores_copy_data() {
        return [
            {tag: "private-note", field: "requestPaymentNoteSelfInput"},
            {tag: "message", field: "requestPaymentMessageInput"},
            {tag: "recipient", field: "requestPaymentYourNameInput"},
        ]
    }
    function test_empty_request_reverted_edit_restores_copy(data) {
        const page = createPage()
        page.draftCard.createRequest()
        tryCompare(page.requestModal, "opened", true)
        const card = page.requestModal.card
        const copy = findChild(card, "requestPaymentCopyButton")
        const update = findChild(card, "requestPaymentUpdateButton")
        verify(copy.visible)

        editField(card, data.field, "Temporary detail")
        verify(card.modified)
        verify(update.enabled)
        verify(!copy.visible)
        editField(card, data.field, "")
        verify(!card.modified)
        verify(!update.visible)
        verify(copy.visible)
        verify(copy.enabled)
        verify(!findChild(card, "requestPaymentError").visible)
        compare(card.request.noteSelf, "")
        compare(card.request.message, "")
        compare(card.request.label, "")
    }

    function editField(card, objectName, text) {
        if (!card.modalView && objectName === "requestPaymentYourNameInput") card.showRecipientName = true
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

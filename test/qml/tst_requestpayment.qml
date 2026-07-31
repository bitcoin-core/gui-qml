// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtTest 1.2
import "../../qml/controls/utils.js" as Utils
import "../../qml/pages/wallet"

TestCase {
    name: "RequestPayment"
    when: windowShown
    width: 900
    height: 700

    Component {
        id: requestPaymentComponent

        RequestPayment {}
    }

    function init() {
        testPaymentRequest.clear()
        testWalletModel.lastCommitAddressType = ""
        testWalletModel.lastTemplateRequestId = ""
        testWalletModel.lastRemovedRequestId = ""
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

    function test_amountInput_keeps_user_draft_while_model_display_updates() {
        const page = createTemporaryObject(requestPaymentComponent, this)
        verify(page !== null)
        page.wallet = testWalletModel
        page.request = testPaymentRequest

        const amountInput = findChild(page, "requestPaymentAmountInput")
        verify(amountInput !== null)

        amountInput.text = ""
        amountInput.forceActiveFocus()
        verify(amountInput.activeFocus)
        keyClick("1")

        compare(amountInput.text, "1")
        compare(testPaymentRequest.amount.display, "1.00000000")
    }

    function test_amountInput_allows_editing_whole_part_before_decimal() {
        const page = createTemporaryObject(requestPaymentComponent, this)
        verify(page !== null)
        page.wallet = testWalletModel
        page.request = testPaymentRequest

        const amountInput = findChild(page, "requestPaymentAmountInput")
        verify(amountInput !== null)

        amountInput.text = "0.00000000"
        amountInput.cursorPosition = 1
        amountInput.forceActiveFocus()
        verify(amountInput.activeFocus)
        keyClick("1")

        compare(amountInput.text, "01.00000000")
        compare(testPaymentRequest.amount.display, "1.00000000")

        amountInput.focus = false
        wait(0)
        compare(amountInput.activeFocus, false)
        compare(amountInput.text, "1.00000000")
    }

    function test_amountInput_rejects_extra_decimal_digits_while_editing() {
        const page = createTemporaryObject(requestPaymentComponent, this)
        verify(page !== null)
        page.wallet = testWalletModel
        page.request = testPaymentRequest

        const amountInput = findChild(page, "requestPaymentAmountInput")
        verify(amountInput !== null)

        testPaymentRequest.amount.display = "0.12345678"
        tryCompare(amountInput, "text", "0.12345678")
        amountInput.cursorPosition = amountInput.text.length
        amountInput.forceActiveFocus()
        verify(amountInput.activeFocus)
        keyClick("9")

        compare(amountInput.text, "0.12345678")
        compare(testPaymentRequest.amount.display, "0.12345678")

        amountInput.focus = false
        wait(0)
        compare(amountInput.activeFocus, false)
        compare(amountInput.text, "0.12345678")
    }

    function test_amountInput_rejects_extra_decimal_points_while_editing() {
        const page = createTemporaryObject(requestPaymentComponent, this)
        verify(page !== null)
        page.wallet = testWalletModel
        page.request = testPaymentRequest

        const amountInput = findChild(page, "requestPaymentAmountInput")
        verify(amountInput !== null)

        testPaymentRequest.amount.display = "1.20000000"
        amountInput.text = "1.2"
        amountInput.cursorPosition = amountInput.text.length
        amountInput.forceActiveFocus()
        verify(amountInput.activeFocus)
        keyClick(".")

        compare(amountInput.text, "1.2")
        compare(testPaymentRequest.amount.display, "1.20000000")
    }

    function test_addressTypeSelection_passes_selected_type_to_generation() {
        const page = createTemporaryObject(requestPaymentComponent, this)
        verify(page !== null)
        page.wallet = testWalletModel
        page.request = testPaymentRequest

        const addressTypeToggle = findChild(page, "receiveOptionsAddressTypeToggle")
        verify(addressTypeToggle !== null)
        addressTypeToggle.checked = true

        const picker = findChild(page, "receiveAddressTypePicker")
        verify(picker !== null)
        compare(page.showAddressTypeSelector, true)
        page.selectedReceiveAddressType = "p2sh-segwit"
        compare(picker.selectedLabel, "Base58 (P2SH-SegWit)")

        const generateButton = findChild(page, "requestPaymentGenerateButton")
        verify(generateButton !== null)
        generateButton.clicked()

        compare(testPaymentRequest.addressType, "p2sh-segwit")
        compare(testWalletModel.lastCommitAddressType, "p2sh-segwit")
    }

    function test_addressTypePicker_highlight_is_tight_to_label() {
        const page = createTemporaryObject(requestPaymentComponent, this)
        verify(page !== null)
        page.wallet = testWalletModel
        page.request = testPaymentRequest

        const addressTypeToggle = findChild(page, "receiveOptionsAddressTypeToggle")
        verify(addressTypeToggle !== null)
        addressTypeToggle.checked = true

        const picker = findChild(page, "receiveAddressTypePicker")
        verify(picker !== null)
        page.selectedReceiveAddressType = "p2sh-segwit"

        tryVerify(function() { return picker.width > 0 })
        verify(picker.width < 300)
        compare(picker.height, 30)
        compare(picker.selectedLabel, "Base58 (P2SH-SegWit)")
    }

    function test_createdRequestOptions_include_template_and_delete_actions() {
        const page = createTemporaryObject(requestPaymentComponent, this)
        verify(page !== null)
        page.wallet = testWalletModel
        page.request = testPaymentRequest

        testWalletModel.commitPaymentRequest()

        const popup = findChild(page, "receiveOptionsPopup")
        verify(popup !== null)
        compare(popup.showRequestActions, true)
        popup.open()
        tryCompare(popup, "opened", true)

        const templateButton = findChild(page, "receiveOptionsUseAsTemplateButton")
        verify(templateButton !== null)
        verify(templateButton.visible)

        templateButton.clicked()
        compare(testWalletModel.lastTemplateRequestId, "1")
        compare(testPaymentRequest.id, "")
        compare(testPaymentRequest.address, "")
        compare(testPaymentRequest.isEditing, true)
    }

    function test_viewAddressHistoryOption_emits_navigation_request() {
        const page = createTemporaryObject(requestPaymentComponent, this)
        verify(page !== null)
        page.wallet = testWalletModel
        page.request = testPaymentRequest

        let addressHistoryRequests = 0
        page.addressHistoryRequested.connect(function() {
            ++addressHistoryRequests
        })

        const popup = findChild(page, "receiveOptionsPopup")
        verify(popup !== null)
        popup.open()
        tryCompare(popup, "opened", true)

        const viewHistoryButton = findChild(page, "receiveOptionsViewAddressHistoryButton")
        verify(viewHistoryButton !== null)
        verify(viewHistoryButton.enabled)

        viewHistoryButton.clicked()
        compare(addressHistoryRequests, 1)
        tryCompare(popup, "opened", false)
    }

    function test_createdRequestDeleteAction_removes_and_clears_request() {
        const page = createTemporaryObject(requestPaymentComponent, this)
        verify(page !== null)
        page.wallet = testWalletModel
        page.request = testPaymentRequest

        testWalletModel.commitPaymentRequest()

        const popup = findChild(page, "receiveOptionsPopup")
        verify(popup !== null)
        popup.open()
        tryCompare(popup, "opened", true)

        const deleteButton = findChild(page, "receiveOptionsDeleteFromHistoryButton")
        verify(deleteButton !== null)
        verify(deleteButton.visible)

        deleteButton.clicked()
        compare(testWalletModel.lastRemovedRequestId, "1")
        compare(walletController.closePaymentRequestDetailRequests, 1)
        compare(testPaymentRequest.id, "")
        compare(testPaymentRequest.address, "")
        compare(testPaymentRequest.isEditing, true)
    }

    function test_editingRequestTemplateAction_cancels_edit_and_fills_template() {
        const page = createTemporaryObject(requestPaymentComponent, this)
        verify(page !== null)
        page.wallet = testWalletModel
        page.request = testPaymentRequest

        testPaymentRequest.label = "Alice"
        testPaymentRequest.message = "Coffee"
        testPaymentRequest.noteSelf = "Counter"
        testPaymentRequest.addressType = "p2sh-segwit"
        testPaymentRequest.amount.display = "0.00200000"
        testWalletModel.commitPaymentRequest()
        testPaymentRequest.edit()
        testPaymentRequest.label = "Unsaved name"
        testPaymentRequest.message = "Unsaved message"
        testPaymentRequest.noteSelf = "Unsaved note"
        testPaymentRequest.amount.display = "0.00300000"

        const popup = findChild(page, "receiveOptionsPopup")
        verify(popup !== null)
        compare(popup.showRequestActions, true)
        popup.open()
        tryCompare(popup, "opened", true)

        const templateButton = findChild(page, "receiveOptionsUseAsTemplateButton")
        verify(templateButton !== null)
        verify(templateButton.visible)

        templateButton.clicked()
        compare(testWalletModel.lastTemplateRequestId, "1")
        compare(testPaymentRequest.id, "")
        compare(testPaymentRequest.address, "")
        compare(testPaymentRequest.isEditing, true)
        compare(testPaymentRequest.label, "Alice")
        compare(testPaymentRequest.message, "Coffee")
        compare(testPaymentRequest.noteSelf, "Counter")
        compare(testPaymentRequest.addressType, "p2sh-segwit")
        compare(page.selectedReceiveAddressType, "p2sh-segwit")
        compare(testPaymentRequest.amount.display, "0.00200000")
    }

    function test_lockedEditorNameFollowsExternalLabelChange() {
        const page = createTemporaryObject(requestPaymentComponent, this)
        verify(page !== null)
        page.wallet = testWalletModel
        page.request = testPaymentRequest

        const nameField = findChild(page, "requestPaymentYourNameInput")
        verify(nameField !== null)

        // Typing replaces the field's declarative binding, like a real session.
        nameField.text = "Alice"
        testPaymentRequest.label = "Alice"
        testWalletModel.commitPaymentRequest()
        compare(nameField.text, "Alice")

        // An Addresses page edit reverse-syncs the held request's label while
        // the form is locked; the field must follow.
        testPaymentRequest.label = "Bob"
        compare(nameField.text, "Bob")

        // Mid-edit, an external label change must not clobber the draft.
        testPaymentRequest.edit()
        nameField.text = "Unsaved draft"
        testPaymentRequest.label = "Carol"
        compare(nameField.text, "Unsaved draft")
    }

    function test_editingRequestDeleteAction_removes_and_clears_request() {
        const page = createTemporaryObject(requestPaymentComponent, this)
        verify(page !== null)
        page.wallet = testWalletModel
        page.request = testPaymentRequest

        testWalletModel.commitPaymentRequest()
        testPaymentRequest.edit()

        const popup = findChild(page, "receiveOptionsPopup")
        verify(popup !== null)
        compare(popup.showRequestActions, true)
        popup.open()
        tryCompare(popup, "opened", true)

        const deleteButton = findChild(page, "receiveOptionsDeleteFromHistoryButton")
        verify(deleteButton !== null)
        verify(deleteButton.visible)

        deleteButton.clicked()
        compare(testWalletModel.lastRemovedRequestId, "1")
        compare(walletController.closePaymentRequestDetailRequests, 1)
        compare(testPaymentRequest.id, "")
        compare(testPaymentRequest.address, "")
        compare(testPaymentRequest.isEditing, true)
    }
}

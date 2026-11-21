// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Window 2.15
import QtTest 1.2
import org.bitcoincore.qt 1.0
import "../../qml/components"
import "../../qml/pages/wallet"

TestCase {
    id: testCase
    name: "TypedReceive"
    when: windowShown
    width: 840
    height: 820
    property var views: []
    function init() { testSetup.failOnWarnings(); testWalletModel.setReceive(null); optionsModel.displayUnit = BitcoinAmount.BTC; Clipboard.currentText = "unchanged external clipboard" }
    function cleanup() {
        testWalletModel.setReceive(null)
        for (let index = 0; index < views.length; ++index) {
            const receipt = testSetup.watchDestruction(views[index])
            views[index].destroy()
            tryCompare(receipt, "pending", 0)
            testSetup.releaseDestructionWatch(receipt)
        }
        views = []
        optionsModel.displayUnit = BitcoinAmount.BTC
        Clipboard.currentText = ""
    }
    Component {
        id: workspaceComponent
        ReceiveWorkspace {
            width: 700
            height: 700
            wallet: testWalletModel
            property int navigationEffects: 0
            onPaymentRequestCreated: navigationEffects++
        }
    }
    Component {
        id: viewComponent
        Item {
            id: view
            width: Math.min(740, testCase.Window.window.contentItem.width)
            height: Math.min(780, testCase.Window.window.contentItem.height)
            property alias control: control
            property alias card: card
            property alias flickable: scroll.contentItem
            property bool saved: false
            ControlledReceiveFixture { id: control }
            ScrollView {
                id: scroll
                anchors.fill: parent
                clip: true
                contentWidth: availableWidth
                contentHeight: card.implicitHeight
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ReceiveRequestEditorCard {
                    id: card
                    width: scroll.availableWidth
                    receive: control.receive
                    editor: view.saved ? control.savedEditor : control.receive.editor
                }
            }
        }
    }
    function makeView(saved) {
        const view = createTemporaryObject(viewComponent, testCase.Window.window.contentItem, {saved: !!saved})
        verify(view !== null)
        views.push(view)
        view.control.settle()
        if (saved) verify(view.control.openSaved())
        else verify(view.control.receive.ensureAddress())
        view.control.settle()
        verify(waitForRendering(view))
        verify(view.card.editor !== null)
        return view
    }
    function field(view, name) { const item = findChild(view, name); verify(item !== null, name); return item }
    function clickVisible(view, name) {
        const item = field(view, name)
        const flickable = view.flickable
        if (flickable && flickable.contentItem) {
            const point = item.mapToItem(flickable.contentItem, item.width / 2, item.height / 2)
            flickable.contentY = Math.max(0, Math.min(flickable.contentHeight - flickable.height, point.y - flickable.height / 2))
        }
        tryVerify(function() {
            if (!item.visible || !item.enabled || item.width <= 0 || item.height <= 0) return false
            const window = item.Window.window
            const center = item.mapToItem(window.contentItem, item.width / 2, item.height / 2)
            if (center.x < 0 || center.y < 0 || center.x >= window.width || center.y >= window.height) return false
            for (let parent = item.parent; parent; parent = parent.parent) {
                if (parent.clip) {
                    const point = item.mapToItem(parent, item.width / 2, item.height / 2)
                    if (point.x < 0 || point.y < 0 || point.x >= parent.width || point.y >= parent.height) return false
                }
            }
            return true
        }, 5000, name + " center must be visible through every clip")
        mouseClick(item, item.width / 2, item.height / 2)
    }
    function edit(view, name, text) { verify(testSetup.replaceText(field(view, name), text)); view.control.settle() }
    function test_current_credit_is_distinct_from_historical_use_data() {
        return [{tag: "zero-after-cancel", current: 0, status: "Awaiting payment"},
                {tag: "positive-underpayment", current: 546, status: "Payment received"}]
    }
    function test_current_credit_is_distinct_from_historical_use(data) {
        const view = makeView(true)
        view.control.publishCredit(1000, true)
        view.control.settle()
        compare(view.card.editor.receipts.receivedAmountSatoshi, 1000)
        compare(field(view, "savedRequestReceiptStatus").text, "Payment received")
        verify(!field(view, "savedRequestCopyUriButton").enabled)
        view.control.publishCredit(data.current, true)
        view.control.settle()
        compare(view.card.editor.receipts.receivedAmountSatoshi, data.current)
        verify(view.card.editor.receipts.fresh)
        verify(view.card.editor.receipts.historicallyUsed)
        compare(field(view, "savedRequestReceiptStatus").text, data.status)
        compare(field(view, "savedRequestHistoricalUseNotice").visible, data.current === 0)
        verify(!field(view, "savedRequestCopyUriButton").enabled)
        compare(view.control.clipboardWrites, 0)
        compare(view.control.writes, 0)
        compare(view.card.editor.requestId, "17")
        if (data.current > 0) verify(field(view, "savedRequestReceivedAmount").text.indexOf("0.00000546") >= 0)
    }
    function test_held_receipt_reconciliation_preserves_amount_without_claiming_freshness() {
        const view = makeView(true)
        view.control.publishCredit(1000, true)
        view.control.settle()
        view.control.invalidateCredit()
        verify(!view.card.editor.receipts.fresh)
        compare(view.card.editor.receipts.receivedAmountSatoshi, 1000)
        compare(field(view, "savedRequestReceiptStatus").text, "Checking payments…")
        verify(!field(view, "savedRequestReceivedAmount").visible)
        view.control.publishCredit(0, true)
        view.control.settle()
        verify(view.card.editor.receipts.fresh)
        compare(field(view, "savedRequestReceiptStatus").text, "Awaiting payment")
        verify(!field(view, "savedRequestCopyUriButton").enabled)
        compare(view.control.clipboardWrites, 0)
    }
    function test_receipt_failure_does_not_paint_a_zero_payment_as_current() {
        const view = makeView(true)
        view.control.publishCredit(546, true)
        view.control.settle()
        view.control.failCredit()
        verify(!view.card.editor.receipts.fresh)
        compare(view.card.editor.receipts.receivedAmountSatoshi, 546)
        compare(field(view, "savedRequestReceiptStatus").text, view.card.editor.receipts.errorText)
        verify(field(view, "savedRequestRetryPaymentsButton").visible)
        view.control.publishCredit(546, true)
        view.control.settle()
        verify(view.card.editor.receipts.fresh)
        compare(field(view, "savedRequestReceiptStatus").text, "Payment received")
        verify(!field(view, "savedRequestRetryPaymentsButton").visible)
        compare(view.control.writes, 0)
    }
    function test_physical_input_is_the_captured_saved_request() {
        const view = makeView(false)
        edit(view, "receiveMessageInput", "a public message")
        edit(view, "receivePrivateNoteInput", "a private note")
        edit(view, "receiveAmountInput", "1.25")
        clickVisible(view, "receiveSaveRequestButton")
        compare(view.control.writes, 0)
        view.control.settle()
        compare(view.control.writes, 1)
        compare(view.control.persistedAmount, 125000000)
        compare(view.control.persistedMessage, "a public message")
        compare(view.control.persistedNote, "a private note")
        verify(view.card.editor.saveResult.requestSaved)
    }
    function test_detached_creation_keeps_its_actual_save_without_navigation_data() {
        return [{tag: "hidden-page", deactivate: false}, {tag: "inactive-wallet-view", deactivate: true}]
    }
    function test_detached_creation_keeps_its_actual_save_without_navigation(data) {
        const view = makeView(false)
        testWalletModel.setReceive(view.control.receive)
        const workspace = createTemporaryObject(workspaceComponent, testCase.Window.window.contentItem)
        verify(workspace !== null)
        views.push(workspace)
        view.control.settle()
        view.card.editor.message.edit("accepted by the original visible view")
        view.card.editor.save()
        if (data.deactivate) workspace.walletViewActive = false
        else workspace.visible = false
        view.control.settle()
        compare(view.control.writes, 1)
        verify(view.card.editor.saveResult.requestSaved)
        compare(view.control.persistedMessage, "accepted by the original visible view")
        compare(workspace.navigationEffects, 0)
    }
    function test_unit_change_before_save_preserves_canonical_amount_data() { return [{tag: "own-unit-button", physical: true}, {tag: "other-view-unit", physical: false}] }
    function test_unit_change_before_save_preserves_canonical_amount(data) {
        const view = makeView(false)
        edit(view, "receiveAmountInput", "1.25")
        if (data.physical) clickVisible(view, "receiveAmountUnitButton")
        else optionsModel.displayUnit = BitcoinAmount.SAT
        compare(view.card.editor.amount.satoshi, 125000000)
        clickVisible(view, "receiveSaveRequestButton")
        view.control.settle()
        compare(view.control.writes, 1)
        compare(view.control.persistedAmount, 125000000)
    }
    function test_invalid_amount_never_submits_the_previous_value() {
        const view = makeView(false)
        edit(view, "receiveAmountInput", "1.25")
        edit(view, "receiveAmountInput", "1..2")
        compare(field(view, "receiveSaveRequestButton").enabled, false)
        compare(view.control.writes, 0)
        verify(!view.card.editor.valid)
        compare(view.card.editor.amount.inputText, "1..2")
    }
    function test_rejected_type_cannot_submit_the_old_displayed_address() {
        const view = makeView(false)
        const oldAddress = view.card.editor.address
        view.control.setNextGenerationError(ReceiveError.Unsupported)
        verify(view.control.receive.chooseAddressType("legacy"))
        compare(field(view, "receiveSaveRequestButton").enabled, false)
        view.control.settle()
        compare(view.control.receive.reservation.address, oldAddress)
        verify(!view.control.receive.reservation.available)
        compare(view.control.writes, 0)
    }
    function test_used_reservation_keeps_composition_until_explicit_next() {
        const view = makeView(false)
        edit(view, "receiveMessageInput", "do not silently rebind")
        const oldAddress = view.card.editor.address
        view.control.markDraftUsed()
        view.control.settle()
        compare(view.control.generations, 1)
        compare(view.card.editor.address, oldAddress)
        compare(view.card.editor.message.text, "do not silently rebind")
        verify(!view.card.editor.canSave)
        verify(view.control.receive.nextAddress())
        view.control.settle()
        compare(view.control.generations, 2)
        compare(view.card.editor.message.text, "do not silently rebind")
    }
    function test_close_question_keeps_or_discards_exact_original_editor_data() { return [{tag: "keep", button: "receiveKeepEditingButton", replace: false}, {tag: "discard", button: "receiveDiscardCloseButton", replace: true}] }
    function test_close_question_keeps_or_discards_exact_original_editor(data) {
        const view = makeView(false)
        edit(view, "receiveMessageInput", "original draft")
        const editorId = view.card.editor.editorId
        verify(view.control.receive.useAddress("bcrt1q-selected"))
        view.control.settle()
        clickVisible(view, data.button)
        view.control.settle()
        compare(view.card.editor.editorId !== editorId, data.replace)
        compare(view.control.receive.replacementPending, false)
        compare(view.control.writes, 0)
        if (!data.replace) compare(view.card.editor.message.text, "original draft")
    }
    function test_new_editor_does_not_inherit_the_previous_native_undo_context() {
        const view = makeView(false)
        edit(view, "receiveAmountInput", "1.25")
        const oldEditorId = view.card.editor.editorId
        const oldField = field(view, "receiveAmountInput")
        verify(view.control.receive.newRequest())
        view.control.settle()
        clickVisible(view, "receiveDiscardCloseButton")
        view.control.settle()
        verify(view.card.editor.editorId !== oldEditorId)
        const current = field(view, "receiveAmountInput")
        verify(current !== oldField)
        current.forceActiveFocus()
        keyClick(Qt.Key_Z, Qt.ControlModifier)
        view.control.settle()
        compare(view.card.editor.amount.satoshi, 0)
        compare(current.text, "")
        compare(view.control.writes, 0)
    }
    // #1083: a blank amount edit followed by a blank next request leaves no
    // binding change to reset the field, so only a new field drops the undo.
    function test_blank_amount_edit_cannot_be_undone_into_the_next_blank_request() {
        const view = makeView(false)
        edit(view, "receiveAmountInput", "0.00421")
        const oldEditorId = view.card.editor.editorId
        const oldField = field(view, "receiveAmountInput")
        oldField.forceActiveFocus()
        oldField.selectAll()
        keyClick(Qt.Key_Backspace)
        view.control.settle()
        compare(oldField.text, "")
        compare(view.card.editor.amount.satoshi, 0)
        verify(view.control.receive.newRequest())
        view.control.settle()
        const discard = findChild(view, "receiveDiscardCloseButton")
        if (discard && discard.visible) { clickVisible(view, "receiveDiscardCloseButton"); view.control.settle() }
        tryVerify(function() { return view.card.editor !== null && view.card.editor.editorId !== oldEditorId })
        compare(view.card.editor.amount.satoshi, 0)
        const current = field(view, "receiveAmountInput")
        compare(current.text, "")
        current.forceActiveFocus()
        keyClick(Qt.Key_Z, Qt.ControlModifier)
        view.control.settle()
        compare(current.text, "")
        compare(view.card.editor.amount.satoshi, 0)
        compare(view.card.editor.amount.inputText, "")
        compare(view.control.writes, 0)
    }
    function test_copy_uses_saved_owned_payload_without_scene_capture() {
        const view = makeView(true)
        view.control.settle()
        clickVisible(view, "savedRequestCopyUriButton")
        view.control.backend()
        compare(view.control.clipboardWrites, 0)
        view.control.publish()
        compare(view.control.clipboardWrites, 1)
        compare(view.control.copiedText, "bitcoin:bcrt1q-original?amount=0.000001&message=original%20message")
        compare(Clipboard.text(), "unchanged external clipboard")
    }
    function test_hide_revokes_a_queued_copy_before_native_handoff() {
        const view = makeView(true)
        view.control.settle()
        clickVisible(view, "savedRequestCopyUriButton")
        const original = view.card.sharing.lastOperation
        verify(original !== null)
        view.control.backend()
        view.visible = false
        view.control.publish()
        compare(view.control.clipboardWrites, 0)
        verify(original.result.present)
        verify(!original.result.performed)
    }
    function test_paid_saved_public_fields_freeze_but_private_note_saves() {
        const view = makeView(true)
        view.control.markSavedUsed()
        view.control.settle()
        compare(field(view, "receiveMessageInput").readOnly, true)
        compare(field(view, "receivePrivateNoteInput").readOnly, false)
        edit(view, "receivePrivateNoteInput", "private after payment")
        clickVisible(view, "receiveSaveRequestButton")
        view.control.settle()
        compare(view.control.persistedMessage, "original message")
        compare(view.control.persistedNote, "private after payment")
        compare(view.control.writes, 1)
        compare(field(view, "savedRequestCopyUriButton").enabled, false)
    }
    function test_persistence_error_keeps_the_original_field_intent_visible() {
        const view = makeView(false)
        edit(view, "receiveMessageInput", "keep after failure")
        view.control.setNextWriteError(ReceiveError.Persistence)
        clickVisible(view, "receiveSaveRequestButton")
        view.control.settle()
        verify(field(view, "receiveEditorError").text.length > 0)
        compare(view.card.editor.message.text, "keep after failure")
        verify(view.card.editor.dirty)
        verify(!view.card.editor.saveResult.requestSaved)
        compare(view.control.writes, 1)
    }
}

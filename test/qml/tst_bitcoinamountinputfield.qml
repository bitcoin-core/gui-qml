// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.2
import org.bitcoincore.qt 1.0
import "../../qml/controls"

TestCase {
    id: testCase
    name: "BitcoinAmountInputField"
    when: windowShown
    width: 760
    height: 220
    property var editors: []

    Component {
        id: editorComponent
        BitcoinAmountInputField {
            id: editor
            property BitcoinAmount originalAmount: BitcoinAmount { unit: optionsModel.displayUnit }
            property bool hideAfterEdit: false
            property BitcoinAmount replacementAfterEdit: null
            property int textEditCount: 0
            property int inputChangeCount: 0
            property int uriPasteCount: 0
            property int editingFinishCount: 0
            property string lastFinishedText: ""
            property string lastPastedUri: ""
            amount: originalAmount
            interceptPaste: true
            inputObjectName: "standaloneAmountInput"
            unitToggleObjectName: "standaloneAmountUnit"
            unitLabelObjectName: "standaloneAmountUnitLabel"
            errorTextObjectName: "standaloneAmountError"
            width: Math.min(700, testCase.Window.window.contentItem.width)
            height: implicitHeight
            y: 20
            onInputTextChanged: inputChangeCount++
            onTextEdited: {
                textEditCount++
                if (hideAfterEdit) visible = false
                if (replacementAfterEdit) amount = replacementAfterEdit
            }
            onEditingFinished: (value) => {
                editingFinishCount++
                lastFinishedText = value
            }
            onPaymentUriPasted: (text) => {
                uriPasteCount++
                lastPastedUri = text
            }
        }
    }

    Component {
        id: amountComponent
        BitcoinAmount { unit: optionsModel.displayUnit }
    }

    function init() {
        testSetup.failOnWarnings()
        optionsModel.displayUnit = BitcoinAmount.BTC
        Clipboard.currentText = ""
    }

    function cleanup() {
        for (let i = 0; i < editors.length; ++i) {
            const receipt = testSetup.watchDestruction(editors[i])
            editors[i].destroy()
            tryCompare(receipt, "pending", 0)
            testSetup.releaseDestructionWatch(receipt)
        }
        editors = []
        optionsModel.displayUnit = BitcoinAmount.BTC
        Clipboard.currentText = ""
    }

    function createEditor(properties) {
        const editor = createTemporaryObject(editorComponent, testCase.Window.window.contentItem, properties || {})
        verify(editor !== null)
        editors.push(editor)
        editor.visible = true
        verify(waitForRendering(editor))
        return editor
    }

    function replacementModel(editor) {
        const amount = createTemporaryObject(amountComponent, editor)
        verify(amount !== null)
        amount.satoshi = 200000000
        return amount
    }

    function clickUnit(editor, requireEnabled) {
        const button = findChild(editor, "standaloneAmountUnit")
        verify(button !== null && button.visible)
        compare(button.enabled, requireEnabled)
        const window = button.Window.window
        const point = button.mapToItem(window.contentItem, button.width / 2, button.height / 2)
        verify(point.x >= 0 && point.x < window.contentItem.width)
        verify(point.y >= 0 && point.y < window.contentItem.height)
        window.update()
        verify(waitForRendering(button))
        mouseClick(button, button.width / 2, button.height / 2)
    }

    function pasteAll(editor, text) {
        editor.field.forceActiveFocus()
        tryCompare(editor.field, "activeFocus", true)
        editor.field.selectAll()
        Clipboard.currentText = text
        editor.paste(false)
    }

    function snapshot(amount) {
        return {satoshi: amount.satoshi, input: amount.inputText, state: amount.inputState, dirty: amount.inputDirty}
    }

    function compareUnchanged(amount, before) {
        compare(amount.satoshi, before.satoshi)
        compare(amount.inputText, before.input)
        compare(amount.inputState, before.state)
        compare(amount.inputDirty, before.dirty)
    }

    function test_unit_placeholder_and_label_data() {
        return [
            {tag: "BTC", unit: BitcoinAmount.BTC, placeholder: "0.00000000", label: "BTC", present: true},
            {tag: "mBTC", unit: BitcoinAmount.MilliBTC, placeholder: "0.00000", label: "mBTC", present: true},
            {tag: "bits", unit: BitcoinAmount.MicroBTC, placeholder: "0.00", label: "bits", present: true},
            {tag: "sats", unit: BitcoinAmount.SAT, placeholder: "0", label: "sats", present: true},
            {tag: "absent-model", unit: BitcoinAmount.BTC, placeholder: "0.00000000", label: "BTC", present: false}
        ]
    }
    function test_unit_placeholder_and_label(data) {
        optionsModel.displayUnit = data.unit
        const editor = createEditor(data.present ? {} : {amount: null})
        compare(editor.placeholderText, data.placeholder)
        compare(editor.field.placeholderText, data.placeholder)
        compare(findChild(editor, "standaloneAmountUnitLabel").text, data.label)
        compare(findChild(editor, "standaloneAmountUnit").enabled, data.present)
        if (data.present) {
            compare(editor.amount.unit, data.unit)
            editor.amount.editInput("1")
            compare(editor.field.text, "1")
            compare(editor.amount.satoshi, data.unit === BitcoinAmount.BTC ? 100000000
                : data.unit === BitcoinAmount.MilliBTC ? 100000 : data.unit === BitcoinAmount.MicroBTC ? 100 : 1)
        } else {
            editor.field.selectAll()
            editor.paste(false)
            editor.syncFromAmount(true)
            compare(editor.field.text, "")
            compare(editor.originalAmount.inputText, "")
        }
        compare(Clipboard.currentText, "")
    }

    function test_sync_from_amount_respects_edit_focus_data() {
        return [{tag: "active-literal", focused: true, force: false, expected: "001.20"},
                {tag: "forced-active-reset", focused: true, force: true, expected: "1.20000000"},
                {tag: "inactive-reset", focused: false, force: false, expected: "1.20000000"}]
    }
    function test_sync_from_amount_respects_edit_focus(data) {
        const editor = createEditor()
        editor.field.forceActiveFocus()
        tryCompare(editor.field, "activeFocus", true)
        if (!data.focused) {
            editor.forceActiveFocus()
            tryCompare(editor.field, "activeFocus", false)
        }
        editor.amount.editInput("001.20")
        compare(editor.field.text, "001.20")
        compare(editor.amount.satoshi, 120000000)
        compare(editor.amount.inputDirty, true)
        editor.syncFromAmount(data.force)
        compare(editor.field.text, data.expected)
        compare(editor.amount.satoshi, 120000000)
        compare(editor.amount.inputDirty, data.focused && !data.force)
    }

    function test_native_edit_routes_only_a_pasted_payment_uri_data() {
        const uri = "bitcoin:bcrt1qexample?amount=0.07"
        return [{tag: "pasted-uri", clip: uri, text: uri, routed: true, kept: true},
                {tag: "typed-uri", clip: "unrelated", text: uri, routed: false, kept: true},
                {tag: "ordinary-number", clip: "1.20", text: "1.20", routed: false, kept: false},
                {tag: "interception-disabled", clip: uri, text: "1.20", routed: false, kept: false, disabled: true}]
    }
    function test_native_edit_routes_only_a_pasted_payment_uri(data) {
        const editor = createEditor({interceptPaste: !data.disabled})
        // Empty, so the native replacement below is a single insertion.
        editor.amount.clear()
        const before = snapshot(editor.amount)
        Clipboard.currentText = data.clip
        verify(testSetup.replaceText(editor.field, data.text))
        compare(editor.uriPasteCount, data.routed ? 1 : 0)
        if (data.routed) compare(editor.lastPastedUri, data.clip)
        if (data.kept) {
            compareUnchanged(editor.amount, before)
            compare(editor.field.text, before.input)
            compare(editor.textEditCount, 0)
        } else {
            compare(editor.amount.inputText, data.text)
            compare(editor.textEditCount, 1)
        }
        compare(Clipboard.currentText, data.clip)
    }

    function test_reentrant_terminal_paste_keeps_applied_original_amount_data() {
        return [{tag: "hide-after-edit", replace: false}, {tag: "replace-after-edit", replace: true}]
    }
    function test_reentrant_terminal_paste_keeps_applied_original_amount(data) {
        const editor = createEditor()
        const original = editor.amount
        original.satoshi = 100000000
        const replacement = replacementModel(editor)
        const replacementBefore = snapshot(replacement)
        editor.hideAfterEdit = !data.replace
        editor.replacementAfterEdit = data.replace ? replacement : null
        pasteAll(editor, "0.07")
        compare(original.satoshi, 7000000)
        compare(original.inputText, "0.07")
        compare(editor.textEditCount, 1)
        compare(editor.inputChangeCount, 1)
        compare(editor.amount, data.replace ? replacement : original)
        compare(editor.visible, data.replace)
        compareUnchanged(replacement, replacementBefore)
    }

    function test_over_long_paste_shows_the_limit_error() {
        const editor = createEditor()
        const amount = editor.amount
        amount.satoshi = 100000000
        pasteAll(editor, "9".repeat(129))
        compare(amount.inputState, BitcoinAmount.Invalid)
        compare(amount.satoshi, 100000000)
        const refusal = "Amount input exceeds the 128-character limit."
        compare(amount.inputError, refusal)
        compare(editor.effectiveErrorText, refusal)
        const error = findChild(editor, "standaloneAmountError")
        compare(error.text, refusal)
        compare(error.visible, true)
        editor.showErrorMessage = false
        compare(error.visible, false)
        compare(editor.effectiveErrorText, refusal)
        editor.showErrorMessage = true
        editor.errorText = "The send form rejected this amount."
        compare(error.text, "The send form rejected this amount.")
        editor.errorText = ""
        compare(error.text, refusal)
        // A later edit replaces the limit error with its own.
        amount.editInput("ordinary-invalid-input")
        compare(editor.effectiveErrorText, amount.inputError)
        verify(editor.effectiveErrorText !== refusal)
        compare(error.text, amount.inputError)
        compare(error.visible, true)
        compare(amount.satoshi, 100000000)
    }

    function test_physical_commit_uses_real_model_or_local_editor_data() {
        return [{tag: "numeric-with-model", present: true, uri: false},
                {tag: "numeric-without-model", present: false, uri: false},
                {tag: "typed-uri-with-model", present: true, uri: true},
                {tag: "typed-uri-without-model", present: false, uri: true}]
    }
    function test_physical_commit_uses_real_model_or_local_editor(data) {
        const editor = createEditor(data.present ? {} : {amount: null})
        const original = editor.originalAmount
        const before = snapshot(original)
        Clipboard.currentText = "unrelated native clipboard"
        const clipboardRevision = Clipboard.revision
        const text = data.uri ? "bitcoin:bcrt1qexample?amount=0.07" : "1.25"
        verify(testSetup.replaceText(editor.field, text))
        compare(editor.field.text, data.uri ? "" : text)
        compare(original.satoshi, data.present && !data.uri ? 125000000 : 0)
        compare(original.inputText, data.present && !data.uri ? text : before.input)
        compare(editor.textEditCount, data.uri ? 0 : 1)
        compare(editor.inputChangeCount, data.uri ? 0 : 1)
        // Typed URI text is not an amount and is not a paste: nothing is routed.
        compare(editor.uriPasteCount, 0)
        compare(Clipboard.currentText, "unrelated native clipboard")
        compare(Clipboard.revision, clipboardRevision)
    }

    function test_physical_unit_toggle_preserves_satoshi_data() {
        return [{tag: "BTC-to-SAT", initial: BitcoinAmount.BTC, target: BitcoinAmount.SAT, expected: "123456789", label: "sats", placeholder: "0"},
                {tag: "SAT-to-BTC", initial: BitcoinAmount.SAT, target: BitcoinAmount.BTC, expected: "1.23456789", label: "BTC", placeholder: "0.00000000"}]
    }
    function test_physical_unit_toggle_preserves_satoshi(data) {
        optionsModel.displayUnit = data.initial
        const editor = createEditor()
        editor.amount.satoshi = 123456789
        Clipboard.currentText = "unit switch keeps clipboard"
        clickUnit(editor, true)
        compare(optionsModel.displayUnit, data.target)
        compare(editor.amount.unit, data.target)
        compare(editor.amount.satoshi, 123456789)
        compare(editor.field.text, data.expected)
        compare(editor.placeholderText, data.placeholder)
        compare(findChild(editor, "standaloneAmountUnitLabel").text, data.label)
        compare(editor.textEditCount, 0)
        compare(Clipboard.currentText, "unit switch keeps clipboard")
    }

    function test_unavailable_paste_and_disabled_unit_have_no_effect_data() {
        return [{tag: "absent-model", absent: true}, {tag: "disabled-editor", absent: false}]
    }
    function test_unavailable_paste_and_disabled_unit_have_no_effect(data) {
        const editor = createEditor(data.absent ? {amount: null} : {enabled: false})
        const original = editor.originalAmount
        original.satoshi = 100000000
        const before = snapshot(original)
        editor.field.selectAll()
        Clipboard.currentText = "0.07"
        editor.paste(false)
        compare(editor.uriPasteCount, 0)
        clickUnit(editor, false)
        compare(optionsModel.displayUnit, BitcoinAmount.BTC)
        compareUnchanged(original, before)
        compare(editor.textEditCount, 0)
    }

    function test_unintercepted_paste_applies_literal_once() {
        const editor = createEditor({interceptPaste: false})
        const amount = editor.amount
        amount.satoshi = 100000000
        editor.field.forceActiveFocus()
        tryCompare(editor.field, "activeFocus", true)
        editor.field.selectAll()
        const before = snapshot(amount)
        Clipboard.currentText = "0.07"
        const clipboardRevision = Clipboard.revision
        editor.paste(false)
        compare(amount.satoshi, 7000000)
        compare(amount.inputText, "0.07")
        compare(editor.field.text, "0.07")
        compare(editor.uriPasteCount, 0)
        compare(editor.textEditCount, 1)
        compare(editor.inputChangeCount, 1)
        compare(Clipboard.currentText, "0.07")
        compare(Clipboard.revision, clipboardRevision)
    }

    function test_model_absent_edit_finish_reports_local_literal() {
        const editor = createEditor({amount: null})
        const before = snapshot(editor.originalAmount)
        verify(testSetup.replaceText(editor.field, "1.25"))
        const finishes = editor.editingFinishCount
        keyClick(Qt.Key_Return)
        compare(editor.editingFinishCount, finishes + 1)
        compare(editor.lastFinishedText, "1.25")
        compare(editor.field.text, "1.25")
        compareUnchanged(editor.originalAmount, before)
        compare(editor.textEditCount, 1)
        compare(editor.inputChangeCount, 1)
        compare(editor.uriPasteCount, 0)
        compare(Clipboard.currentText, "")
    }

    function test_keyboard_paste_applies_a_literal_and_routes_a_payment_uri() {
        const editor = createEditor()
        const original = editor.amount
        original.satoshi = 100000000
        editor.field.forceActiveFocus()
        tryCompare(editor.field, "activeFocus", true)
        editor.field.selectAll()
        Clipboard.currentText = "0.07"
        keyClick(Qt.Key_V, Qt.ControlModifier)
        compare(original.satoshi, 7000000)
        compare(original.inputText, "0.07")
        compare(editor.textEditCount, 1)
        compare(editor.uriPasteCount, 0)
        const before = snapshot(original)
        const uri = "bitcoin:bcrt1qexample?amount=0.09"
        Clipboard.currentText = uri
        keyClick(Qt.Key_V, Qt.ControlModifier)
        compare(editor.uriPasteCount, 1)
        compare(editor.lastPastedUri, uri)
        compareUnchanged(original, before)
        compare(editor.field.text, "0.07")
        compare(editor.textEditCount, 1)
    }

    function test_numeric_key_with_interception_disabled_updates_real_amount() {
        const editor = createEditor({interceptPaste: false})
        editor.amount.clear()
        editor.field.forceActiveFocus()
        tryCompare(editor.field, "activeFocus", true)
        const before = snapshot(editor.amount)
        keyClick("2")
        compare(editor.field.text, "2")
        compare(editor.amount.satoshi, 200000000)
        compare(editor.amount.inputDirty, true)
        compare(editor.textEditCount, 1)
        compare(editor.inputChangeCount, 1)
        compare(editor.uriPasteCount, 0)
        compare(editor.effectiveErrorText, "")
        const finishes = editor.editingFinishCount
        keyClick(Qt.Key_Return)
        compare(editor.field.text, "2.00000000")
        compare(editor.amount.satoshi, 200000000)
        compare(editor.amount.inputDirty, false)
        compare(editor.editingFinishCount, finishes + 1)
        compare(editor.lastFinishedText, "2.00000000")
        compare(editor.textEditCount, 1)
        compare(Clipboard.currentText, "")
    }
}

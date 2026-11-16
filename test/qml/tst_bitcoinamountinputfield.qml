// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.2
import org.bitcoincore.qt 1.0
import "../../qml/controls"

TestCase {
    name: "BitcoinAmountInputField"
    when: windowShown

    Window {
        id: testWindow
        width: 480
        height: 200
        visible: true
    }

    Component {
        id: fieldComponent
        BitcoinAmountInputField {
            width: 440
            amount: BitcoinAmount { unit: BitcoinAmount.BTC }
        }
    }

    function typeAmount(decimalPoint, keys, interceptPaste) {
        const field = createTemporaryObject(fieldComponent, testWindow.contentItem, {
            decimalPoint: decimalPoint,
            interceptPaste: interceptPaste === true
        })
        field.field.forceActiveFocus()
        for (const key of keys) keyClick(key)
        return field
    }

    function test_decimal_comma_locale_reads_comma_as_decimal_point_data() {
        return [
            { tag: "validator", interceptPaste: false },
            { tag: "send", interceptPaste: true },
        ]
    }

    function test_decimal_comma_locale_reads_comma_as_decimal_point(data) {
        const field = typeAmount(",", ["0", ",", "0", "2"], data.interceptPaste)
        compare(field.field.text, "0,02")
        compare(field.amount.satoshi, 2000000)
        field.field.focus = false
        compare(field.amount.satoshi, 2000000)
    }

    function test_decimal_comma_keeps_undo_history() {
        const field = createTemporaryObject(fieldComponent, testWindow.contentItem, { decimalPoint: "," })
        const input = field.field
        input.forceActiveFocus()
        input.text = "7"
        input.cursorPosition = 1
        for (const key of ["1", "2", ","]) keyClick(key)
        compare(input.text, "712,")
        verify(input.canUndo)
        input.undo()
        compare(input.text, "7")
    }

    function test_period_locale_keeps_comma_out_data() {
        return test_decimal_comma_locale_reads_comma_as_decimal_point_data()
    }

    function test_period_locale_keeps_comma_out(data) {
        const field = typeAmount(".", ["1", ",", "5"], data.interceptPaste)
        compare(field.field.text, "15")
        compare(field.amount.satoshi, 1500000000)
    }

    function test_period_is_a_decimal_point_in_every_locale() {
        const field = typeAmount(",", ["0", ".", "5"])
        compare(field.field.text, "0.5")
        compare(field.amount.satoshi, 50000000)
    }
}

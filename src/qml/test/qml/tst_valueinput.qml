// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtTest 1.2
import "../../qml/controls"

TestCase {
    name: "ValueInput"
    when: windowShown
    width: 900
    height: 700

    Component {
        id: valueInputComponent

        ValueInput {
            objectName: "valueInputControl"
            parentState: "ACTIVE"
            description: "250"
            filled: true
        }
    }

    function test_valueInput_has_stable_selector_and_initial_state() {
        const input = createTemporaryObject(valueInputComponent, this)
        verify(input !== null)

        compare(input.objectName, "valueInputControl")
        compare(input.maximumLength, 5)
        compare(input.text, "250")
        compare(input.enabled, true)
    }

    function test_valueInput_checkValidity_bounds() {
        const input = createTemporaryObject(valueInputComponent, this)
        verify(input !== null)

        verify(input.checkValidity(5, 10, 5))
        verify(input.checkValidity(5, 10, 10))
        verify(!input.checkValidity(5, 10, 4))
        verify(!input.checkValidity(5, 10, 11))
        verify(!input.checkValidity(5, 10, parseInt("", 10)))
    }

    function test_valueInput_disabled_state_disables_input() {
        const input = createTemporaryObject(valueInputComponent, this)
        verify(input !== null)

        input.parentState = "DISABLED"
        compare(input.enabled, false)
    }
}

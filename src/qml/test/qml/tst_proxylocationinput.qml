// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtTest 1.2
import "../../qml/controls"

TestCase {
    name: "ProxyLocationInput"
    when: windowShown
    width: 600
    height: 200

    Component {
        id: inputComponent

        ProxyLocationInput {
            parentState: "FILLED"
            description: "127.0.0.1:9050"
            filled: true
            width: 200
            height: 30
        }
    }

    Component {
        id: settingComponent

        Setting {
            id: row
            width: 500
            height: 70
            header: "Proxy location"
            state: "FILLED"
            actionItem: ProxyLocationInput {
                parentState: row.visualState
                description: "127.0.0.1:9050"
                filled: true
            }
            onClicked: loadedItem.beginEdit()
        }
    }

    Component {
        id: initiallyDisabledSettingComponent

        Setting {
            id: row
            width: 500
            height: 70
            header: "Proxy location"
            state: "DISABLED"
            actionItem: ProxyLocationInput {
                parentState: row.visualState
                description: "127.0.0.1:9050"
                filled: true
            }
            onClicked: loadedItem.beginEdit()
        }
    }

    function test_description_syncs_when_not_editing() {
        const input = createTemporaryObject(inputComponent, this)
        verify(input !== null)

        compare(input.text, "127.0.0.1:9050")
        input.description = "10.0.0.1:9050"
        tryCompare(input, "text", "10.0.0.1:9050")
    }

    function test_begin_edit_focuses_without_selecting_value() {
        const input = createTemporaryObject(inputComponent, this)
        verify(input !== null)

        input.beginEdit()
        verify(input.activeFocus)
        compare(input.selectedText, "")
    }

    function test_disabled_input_becomes_editable_when_enabled() {
        const input = createTemporaryObject(inputComponent, this)
        verify(input !== null)

        input.parentState = "DISABLED"
        tryCompare(input, "readOnly", true)
        compare(input.activeFocusOnTab, false)

        input.parentState = "FILLED"
        tryCompare(input, "readOnly", false)
        compare(input.activeFocusOnTab, true)
        input.description = ""
        tryCompare(input, "text", "")

        input.beginEdit()
        verify(input.activeFocus)

        keyClick("1")
        compare(input.text, "1")
    }

    function test_setting_row_activation_enters_edit_mode() {
        const row = createTemporaryObject(settingComponent, this)
        verify(row !== null)
        verify(row.loadedItem !== null)

        row.clicked()
        verify(row.loadedItem.activeFocus)
        compare(row.loadedItem.selectedText, "")
    }

    function test_setting_row_enables_loaded_input_after_disabled_state() {
        const row = createTemporaryObject(settingComponent, this)
        verify(row !== null)
        verify(row.loadedItem !== null)

        row.state = "DISABLED"
        tryCompare(row.loadedItem, "enabled", false)

        row.state = "FILLED"
        tryCompare(row.loadedItem, "enabled", true)
        row.loadedItem.description = ""
        tryCompare(row.loadedItem, "text", "")

        row.clicked()
        verify(row.loadedItem.activeFocus)

        keyClick("1")
        compare(row.loadedItem.text, "1")
    }

    function test_initially_disabled_setting_row_enables_loaded_input() {
        const row = createTemporaryObject(initiallyDisabledSettingComponent, this)
        verify(row !== null)
        verify(row.loadedItem !== null)
        compare(row.loadedItem.parentState, "DISABLED")
        compare(row.loadedItem.readOnly, true)
        compare(row.loadedItem.enabled, false)

        row.state = "FILLED"
        tryCompare(row.loadedItem, "parentState", "FILLED")
        tryCompare(row.loadedItem, "readOnly", false)
        tryCompare(row.loadedItem, "enabled", true)
        row.loadedItem.description = ""
        tryCompare(row.loadedItem, "text", "")

        row.clicked()
        verify(row.loadedItem.activeFocus)

        keyClick("1")
        compare(row.loadedItem.text, "1")
    }
}

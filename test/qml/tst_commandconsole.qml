// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.2
import org.bitcoincore.qt 1.0
import "../../qml/pages/node"

TestCase {
    id: testCase
    name: "CommandConsole"
    when: windowShown
    width: 640
    height: 480

    Component {
        id: consoleComponent

        CommandConsole {
            objectName: "testCommandConsole"
        }
    }

    function init() {
        testRpcConsoleModel.resetForTest()
    }

    function createConsole() {
        const page = createTemporaryObject(consoleComponent, testCase.Window.window.contentItem)
        verify(page !== null)
        page.width = testCase.width
        page.height = testCase.height
        page.visible = true

        const input = findChild(page, "consoleInput")
        verify(input !== null)
        input.forceActiveFocus()
        tryCompare(input, "activeFocus", true)
        return page
    }

    function typeCommand(page, text) {
        const input = findChild(page, "consoleInput")
        input.text = ""
        for (let i = 0; i < text.length; ++i) {
            keyClick(text[i])
        }
        compare(input.text, text)
        return input
    }

    function test_return_runs_the_highlighted_suggestion() {
        const page = createConsole()
        const popup = findChild(page, "consoleAutocompletePopup")
        verify(popup !== null)

        const input = typeCommand(page, "getbl")
        tryCompare(popup, "visible", true)
        compare(page.filteredCommands.length, 2)
        compare(page.autocompleteIndex, 0)

        keyClick(Qt.Key_Return)

        compare(testRpcConsoleModel.submittedCommands.length, 1)
        compare(testRpcConsoleModel.submittedCommands[0], "getblockcount")
        compare(input.text, "")
        tryCompare(popup, "visible", false)
    }

    function test_keypad_enter_submits_typed_command() {
        const page = createConsole()
        const popup = findChild(page, "consoleAutocompletePopup")
        verify(popup !== null)

        const input = typeCommand(page, "foobar")
        compare(popup.visible, false)

        keyClick(Qt.Key_Enter)

        compare(testRpcConsoleModel.submittedCommands.length, 1)
        compare(testRpcConsoleModel.submittedCommands[0], "foobar")
        compare(input.text, "")
    }

    function test_return_keeps_refused_command_in_the_field() {
        const page = createConsole()
        testRpcConsoleModel.setAcceptCommands(false)

        const input = typeCommand(page, "foobar")
        keyClick(Qt.Key_Return)

        compare(testRpcConsoleModel.submittedCommands.length, 0)
        compare(input.text, "foobar")
    }

    function test_tab_fills_in_the_highlighted_suggestion() {
        const page = createConsole()
        const popup = findChild(page, "consoleAutocompletePopup")
        verify(popup !== null)

        const input = typeCommand(page, "getbl")
        tryCompare(popup, "visible", true)

        keyClick(Qt.Key_Tab)

        compare(input.text, "getblockcount ")
        compare(input.cursorPosition, input.text.length)
        compare(testRpcConsoleModel.submittedCommands.length, 0)
        tryCompare(popup, "visible", false)
        compare(input.activeFocus, true)
    }

    function test_tab_without_suggestions_leaves_the_input_alone() {
        const page = createConsole()
        const popup = findChild(page, "consoleAutocompletePopup")
        verify(popup !== null)

        // "foobar" matches nothing, so there is no suggestion to complete.
        const input = typeCommand(page, "foobar")
        compare(popup.visible, false)

        keyClick(Qt.Key_Tab)

        compare(input.text, "foobar")
        compare(testRpcConsoleModel.submittedCommands.length, 0)
    }

    function test_search_mode_ignores_the_submit_keys() {
        const page = createConsole()
        page.toggleSearchMode()
        compare(page.searchMode, true)

        const input = typeCommand(page, "getbl")
        compare(findChild(page, "consoleAutocompletePopup").visible, false)

        keyClick(Qt.Key_Return)
        keyClick(Qt.Key_Enter)
        keyClick(Qt.Key_Tab)

        compare(testRpcConsoleModel.submittedCommands.length, 0)
        compare(input.text, "getbl")
        compare(page.searchDraft, "getbl")
    }
}

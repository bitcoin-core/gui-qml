// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.2
import org.bitcoincore.qt 1.0

import "../../qml/pages/node"

TestCase {
    name: "CommandConsole"
    when: windowShown
    width: 640
    height: 420

    Window {
        id: testWindow
        width: 640
        height: 420
        visible: true
    }

    Component {
        id: consoleComponent

        CommandConsole {
            width: 640
            height: 420
            showHeader: false
            walletName: "w1"
        }
    }

    readonly property int requestCategory: 0
    readonly property int replyCategory: 1

    function init() {
        rpcConsoleModel.resetForTest()
        Clipboard.currentText = ""
    }

    function createPage() {
        rpcConsoleModel.appendForTest(requestCategory, "Executing command using \"w1\" wallet")
        rpcConsoleModel.appendForTest(requestCategory, "getblockcount", "getblockcount", false)
        rpcConsoleModel.appendForTest(replyCategory, "{\n  \"blocks\": 7\n}")
        rpcConsoleModel.appendForTest(requestCategory, "walletpassphrase (…)", "walletpassphrase (…)", true)
        const page = createTemporaryObject(consoleComponent, testWindow.contentItem)
        verify(page !== null)
        const output = findChild(page, "consoleOutputArea")
        tryCompare(output, "count", 6)
        tryVerify(function() { return findChild(page, "consoleOutputArea_content_5") !== null })
        testWindow.requestActivate()
        tryCompare(testWindow, "active", true)
        return page
    }

    function rowPoint(page, row) {
        const content = findChild(page, "consoleOutputArea_content_" + row)
        verify(content !== null)
        return content.mapToItem(page, 4, content.height / 2)
    }

    function openMenuAt(page, row) {
        const menu = findChild(page, "consoleOutputMenu")
        tryCompare(menu, "visible", false)
        const point = rowPoint(page, row)
        mouseClick(page, point.x, point.y, Qt.RightButton)
        tryCompare(menu, "opened", true)
        return menu
    }

    function test_menu_on_a_command_runs_it_again_with_the_current_wallet() {
        const page = createPage()
        const menu = openMenuAt(page, 1)
        const runAgain = findChild(menu, "consoleMenuRunAgain")
        verify(runAgain.visible)
        verify(runAgain.enabled)
        compare(findChild(menu, "consoleMenuCopyEntry").text, "Copy command")
        runAgain.clicked()
        compare(rpcConsoleModel.submitCalls, 1)
        compare(rpcConsoleModel.lastSubmittedCommand, "getblockcount")
        compare(rpcConsoleModel.lastSubmittedWallet, "w1")
        tryCompare(menu, "opened", false)
    }

    function test_edit_puts_the_command_in_the_input_without_autocomplete() {
        const page = createPage()
        const menu = openMenuAt(page, 1)
        findChild(menu, "consoleMenuEdit").clicked()
        const input = findChild(page, "consoleInput")
        compare(input.text, "getblockcount")
        verify(input.activeFocus)
        compare(input.selectedText, "")
        compare(input.cursorPosition, input.text.length)
        compare(findChild(page, "consoleAutocompletePopup").visible, false)
    }

    function test_redacted_commands_cannot_be_run_again() {
        const page = createPage()
        const menu = openMenuAt(page, 5)
        const runAgain = findChild(menu, "consoleMenuRunAgain")
        verify(runAgain.visible)
        verify(!runAgain.enabled)
        verify(findChild(menu, "consoleMenuEdit").visible)
    }

    function test_menu_on_output_copies_the_whole_reply() {
        const page = createPage()
        const menu = openMenuAt(page, 3)
        verify(!findChild(menu, "consoleMenuRunAgain").visible)
        verify(!findChild(menu, "consoleMenuEdit").visible)
        verify(!findChild(menu, "consoleMenuCopy").enabled)
        const copyEntry = findChild(menu, "consoleMenuCopyEntry")
        compare(copyEntry.text, "Copy output")
        copyEntry.clicked()
        compare(Clipboard.currentText, "{\n  \"blocks\": 7\n}")
    }

    function test_wallet_notice_is_not_offered_as_a_command() {
        const page = createPage()
        const menu = openMenuAt(page, 0)
        verify(!findChild(menu, "consoleMenuRunAgain").visible)
    }

    function test_select_entry_select_all_and_copy() {
        const page = createPage()
        const output = findChild(page, "consoleOutputArea")
        let menu = openMenuAt(page, 3)
        findChild(menu, "consoleMenuSelectEntry").clicked()
        compare(output.selectedText(), "{\n  \"blocks\": 7\n}")

        menu = openMenuAt(page, 3)
        const copy = findChild(menu, "consoleMenuCopy")
        verify(copy.enabled)
        copy.clicked()
        compare(Clipboard.currentText, "{\n  \"blocks\": 7\n}")

        menu = openMenuAt(page, 3)
        findChild(menu, "consoleMenuSelectAll").clicked()
        verify(output.selectedText().startsWith("Executing command using"))
        verify(output.selectedText().endsWith("walletpassphrase (…)"))
    }

    function test_right_click_while_the_menu_is_open_moves_it_to_the_new_row() {
        const page = createPage()
        // The menu opens downwards from the click, so the next click goes
        // above it.
        let menu = openMenuAt(page, 3)
        compare(menu.row, 3)
        verify(!findChild(menu, "consoleMenuRunAgain").visible)
        const point = rowPoint(page, 1)
        mouseClick(page, point.x, point.y, Qt.RightButton)
        tryCompare(menu, "row", 1)
        tryCompare(menu, "opened", true)
        verify(findChild(menu, "consoleMenuRunAgain").visible)
    }

    function test_clear_console() {
        const page = createPage()
        const menu = openMenuAt(page, 3)
        findChild(menu, "consoleMenuClear").clicked()
        compare(rpcConsoleModel.clearCalls, 1)
    }

    function test_run_again_is_disabled_while_a_command_executes() {
        const page = createPage()
        rpcConsoleModel.executing = true
        const menu = openMenuAt(page, 1)
        verify(!findChild(menu, "consoleMenuRunAgain").enabled)
    }
}

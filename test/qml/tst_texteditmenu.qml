// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Window 2.15
import QtTest 1.2
import org.bitcoincore.qt 1.0
import "../../qml/controls"

TestCase {
    id: testCase
    name: "TextEditMenu"
    when: windowShown
    width: 500
    height: 400

    function init() {
        testSetup.failOnWarnings()
        Clipboard.currentText = ""
        Clipboard.currentSelectionText = ""
    }

    Component {
        id: harnessComponent
        Item {
            id: harness
            width: 420
            height: 220
            readonly property alias single: single
            readonly property alias multiline: multiline
            readonly property alias editMenu: editMenu
            property var requests: []
            TextField { id: single; width: 400; height: 44; text: "abc def" }
            TextArea { id: multiline; y: 60; width: 400; height: 140; text: "abc\ndef" }
            TextEditMenu {
                id: editMenu
                textInput: single
                menuObjectName: "testEditMenu"
                onPasteRequested: (primary) => harness.requests = harness.requests.concat([primary])
            }
        }
    }
    Component {
        id: labeledComponent
        LabeledTextArea { width: 420; height: 200; text: "prefix old suffix" }
    }

    function fixture(multiline) {
        const harness = createTemporaryObject(harnessComponent, testCase.Window.window.contentItem)
        verify(harness !== null)
        if (multiline) {
            harness.editMenu.textInput = null
            harness.editMenu.textEdit = harness.multiline
        }
        verify(waitForRendering(harness))
        return harness
    }
    function editorRows() { return [{tag: "TextField", multiline: false}, {tag: "TextArea", multiline: true}] }
    function menuItem(harness, text) {
        const menu = harness.editMenu.menu
        for (let i = 0; i < menu.count; ++i) {
            const item = menu.itemAt(i)
            if (item.text === text) return item
        }
        return null
    }

    function test_menu_copy_cut_select_all_and_readonly_data() { return editorRows() }
    function test_menu_copy_cut_select_all_and_readonly(data) {
        const harness = fixture(data.multiline)
        const editor = data.multiline ? harness.multiline : harness.single
        const original = editor.text
        editor.select(0, 3)
        verify(harness.editMenu.editorHasSelection)
        menuItem(harness, "Copy").triggered()
        compare(testSetup.nativeClipboardText(), "abc")
        editor.readOnly = true
        verify(!menuItem(harness, "Cut").enabled)
        verify(!menuItem(harness, "Paste").enabled)
        editor.readOnly = false
        editor.select(0, 3)
        verify(menuItem(harness, "Cut").enabled)
        menuItem(harness, "Cut").triggered()
        compare(editor.text, original.slice(3))
        menuItem(harness, "Select all").triggered()
        compare(editor.selectedText, editor.text)
        menuItem(harness, "Paste").triggered()
        compare(harness.requests, [false])
    }

    function test_middleclick_places_the_cursor_and_requests_primary_paste_data() { return editorRows() }
    function test_middleclick_places_the_cursor_and_requests_primary_paste(data) {
        const harness = fixture(data.multiline)
        const editor = data.multiline ? harness.multiline : harness.single
        const rectangle = editor.positionToRectangle(4)
        // TextInput's rectangle is in layout coordinates; its hit-test uses
        // editor coordinates including Controls padding. TextEdit includes it.
        const x = rectangle.x + (data.multiline ? 0 : editor.leftPadding) + 0.1
        const y = data.multiline ? rectangle.y + rectangle.height / 2 : 10000
        editor.select(0, 2)
        const pointer = {button: Qt.MiddleButton, x: x, y: y, accepted: false}
        harness.editMenu.pointerPressed(pointer)
        verify(pointer.accepted)
        compare(editor.cursorPosition, 4)
        compare(editor.selectedText, "")
        compare(harness.requests, [true])
        harness.editMenu.pasteEnabled = false
        const ignored = {button: Qt.MiddleButton, x: x, y: y, accepted: true}
        harness.editMenu.pointerPressed(ignored)
        verify(!ignored.accepted)
        compare(harness.requests, [true])
    }

    function test_labeled_textarea_pastes_the_literal_at_its_selection() {
        const labeled = createTemporaryObject(labeledComponent, testCase.Window.window.contentItem)
        verify(labeled !== null)
        verify(waitForRendering(labeled))
        const edits = signalSpy.createObject(labeled, {target: labeled, signalName: "textEdited"})
        labeled.field.select(7, 10)
        const literal = "  é\n\t original  "
        Clipboard.currentText = literal
        labeled.paste(false)
        compare(labeled.text, "prefix " + literal + " suffix")
        compare(labeled.field.cursorPosition, 7 + literal.length)
        compare(edits.count, 1)
    }

    function test_intercepted_payment_uri_is_routed_instead_of_inserted() {
        const labeled = createTemporaryObject(labeledComponent, testCase.Window.window.contentItem, {interceptPaste: true,
            paymentUriRestoreText: "prefix old suffix"})
        verify(labeled !== null)
        verify(waitForRendering(labeled))
        const routed = signalSpy.createObject(labeled, {target: labeled, signalName: "paymentUriPasted"})
        Clipboard.currentText = "  bitcoin:bc1qexample?amount=1  "
        labeled.paste(false)
        compare(labeled.text, "prefix old suffix")
        compare(routed.count, 1)
        compare(routed.signalArguments[0][0], "  bitcoin:bc1qexample?amount=1  ")
        Clipboard.currentText = "ordinary"
        labeled.field.cursorPosition = 0
        labeled.paste(false)
        compare(labeled.text, "ordinaryprefix old suffix")
        compare(routed.count, 1)
    }

    Component {
        id: signalSpy
        SignalSpy {}
    }
}

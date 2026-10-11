// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.2
import org.bitcoincore.qt 1.0

import "../../qml/components"

TestCase {
    name: "MonospaceOutputView"
    when: windowShown
    width: 520
    height: 320

    Window {
        id: testWindow
        width: 520
        height: 320
        visible: true
    }

    ListModel {
        id: outputModel

        ListElement { content: "alpha beta" }
        ListElement { content: "<b>ALPHA</b> reply" }
        ListElement { content: "none alpha alpha" }
    }

    ListModel {
        id: multilineOutputModel

        ListElement {
            content: "first line<br>second line<br>third line<br>fourth line<br>final omega"
        }
    }

    Component {
        id: outputComponent

        MonospaceOutputView {
            objectName: "searchOutput"
            width: 480
            height: 120
            listModel: outputModel
            contentTextFormat: Text.RichText
            autoScrollToBottom: false
        }
    }

    Component {
        id: multilineOutputComponent

        MonospaceOutputView {
            objectName: "multilineSearchOutput"
            width: 480
            height: 44
            listModel: multilineOutputModel
            contentTextFormat: Text.RichText
            autoScrollToBottom: false
            topPadding: 0
            bottomPadding: 0
        }
    }

    function createOutput() {
        const output = createTemporaryObject(outputComponent, testWindow.contentItem)
        verify(output !== null)
        tryCompare(output, "count", 3)
        return output
    }

    function test_search_highlights_and_cycles_without_filtering() {
        const output = createOutput()
        const firstRow = findChild(output, "searchOutput_row_0")
        const secondRow = findChild(output, "searchOutput_row_1")
        const thirdRow = findChild(output, "searchOutput_row_2")
        const firstContent = findChild(output, "searchOutput_content_0")
        const secondContent = findChild(output, "searchOutput_content_1")
        const thirdContent = findChild(output, "searchOutput_content_2")
        verify(firstRow !== null)
        verify(secondRow !== null)
        verify(thirdRow !== null)
        verify(firstContent !== null)
        verify(secondContent !== null)
        verify(thirdContent !== null)

        output.searchText = "alpha"
        tryCompare(output, "searchResultCount", 4)
        compare(output.count, 3)
        verify(firstRow.visible)
        verify(secondRow.visible)
        verify(thirdRow.visible)
        verify(firstRow.height > 0)
        verify(secondRow.height > 0)
        verify(thirdRow.height > 0)
        compare(output.currentSearchResultIndex, 0)
        compare(firstContent.selectedText.toLowerCase(), "alpha")

        output.showNextSearchResult()
        compare(output.currentSearchResultIndex, 1)
        compare(firstContent.selectedText, "")
        compare(secondContent.selectedText.toLowerCase(), "alpha")

        output.showPreviousSearchResult()
        compare(output.currentSearchResultIndex, 0)
        compare(firstContent.selectedText.toLowerCase(), "alpha")

        output.showPreviousSearchResult()
        compare(output.currentSearchResultIndex, 3)
        compare(thirdContent.selectedText.toLowerCase(), "alpha")

        output.searchText = ""
        tryCompare(output, "searchResultCount", 0)
        compare(output.currentSearchResultIndex, -1)
        compare(thirdContent.selectedText, "")
        compare(output.count, 3)
    }

    Component {
        id: regressionModelComponent
        ListModel {}
    }

    function test_search_preserves_original_offsets_data() {
        return [
            { tag: "unicode-prefix", text: '"İstanbul savings"', query: "savings", expected: "savings" },
            { tag: "case-insensitive", text: '"İstanbul SAVINGS"', query: "savings", expected: "SAVINGS" },
            { tag: "literal-metacharacters", text: "prefix .*+?^${}()|[]\\ suffix", query: ".*+?^${}()|[]\\", expected: ".*+?^${}()|[]\\" }
        ]
    }

    function test_search_preserves_original_offsets(data) {
        const model = createTemporaryObject(regressionModelComponent, testWindow.contentItem)
        verify(model !== null)
        model.append({ content: data.text })
        const output = createTemporaryObject(outputComponent, testWindow.contentItem, {
            listModel: model
        })
        verify(output !== null)
        tryCompare(output, "count", 1)
        const content = findChild(output, "searchOutput_content_0")
        verify(content !== null)
        compare(content.getText(0, content.length), data.text)

        output.searchText = data.query
        tryCompare(output, "searchResultCount", 1)
        compare(content.selectedText, data.expected)
        compare(content.selectionStart, data.text.indexOf(data.expected))
        compare(content.selectionEnd, data.text.indexOf(data.expected) + data.expected.length)
    }

    function test_search_navigation_owns_scroll_position() {
        const output = createOutput()
        output.height = 30
        output.autoScrollToBottom = true
        output.scrollToBottom()
        verify(output.contentY > 0)

        output.searchText = "alpha"
        tryCompare(output, "searchResultCount", 4)
        const firstMatchY = output.contentY
        wait(100)
        compare(output.currentSearchResultIndex, 0)
        compare(output.contentY, firstMatchY)

        output.showPreviousSearchResult()
        compare(output.currentSearchResultIndex, 3)
        const lastMatchY = output.contentY
        verify(lastMatchY > firstMatchY)
        wait(100)
        compare(output.currentSearchResultIndex, 3)
        compare(output.contentY, lastMatchY)
    }

    // Rows outside the viewport have no delegate, so search has to read the
    // model rather than the instantiated items.
    function test_search_finds_matches_in_rows_never_instantiated() {
        const model = createTemporaryObject(regressionModelComponent, testWindow.contentItem)
        verify(model !== null)
        for (let i = 0; i < 500; ++i) {
            model.append({ content: i === 480 ? "needle here" : "row " + i })
        }
        const output = createTemporaryObject(outputComponent, testWindow.contentItem, {
            listModel: model
        })
        verify(output !== null)
        tryCompare(output, "count", 500)

        output.searchText = "needle"
        tryCompare(output, "searchResultCount", 1)
        compare(output.currentSearchResultIndex, 0)

        const match = findChild(output, "searchOutput_content_480")
        verify(match !== null)
        compare(match.selectedText, "needle")
    }

    function test_appending_rows_follows_the_tail() {
        const model = createTemporaryObject(regressionModelComponent, testWindow.contentItem)
        verify(model !== null)
        const output = createTemporaryObject(outputComponent, testWindow.contentItem, {
            listModel: model,
            autoScrollToBottom: true
        })
        verify(output !== null)

        for (let i = 0; i < 300; ++i) model.append({ content: "line " + i })
        tryCompare(output, "count", 300)
        tryCompare(output, "atBottom", true)

        const lastRow = findChild(output, "searchOutput_content_299")
        verify(lastRow !== null)
        compare(lastRow.getText(0, lastRow.length), "line 299")
    }

    function test_search_scrolls_to_match_inside_multiline_row() {
        const output = createTemporaryObject(multilineOutputComponent,
                                             testWindow.contentItem)
        verify(output !== null)
        tryCompare(output, "count", 1)
        const row = findChild(output, "multilineSearchOutput_row_0")
        const content = findChild(output, "multilineSearchOutput_content_0")
        verify(row !== null)
        verify(content !== null)

        output.searchText = "omega"
        tryCompare(output, "searchResultCount", 1)
        compare(content.selectedText, "omega")

        const matchRect = content.positionToRectangle(content.selectionStart)
        const matchTop = row.y + content.y + matchRect.y
        const matchBottom = matchTop + matchRect.height
        verify(matchRect.y > output.height)
        verify(output.contentY > row.y)
        verify(matchTop >= output.contentY)
        verify(matchBottom <= output.contentY + output.height + 0.5)
    }

    function createTailOutput(rowText) {
        const model = createTemporaryObject(regressionModelComponent, testWindow.contentItem)
        verify(model !== null)
        const output = createTemporaryObject(outputComponent, testWindow.contentItem, {
            listModel: model,
            autoScrollToBottom: true
        })
        verify(output !== null)
        for (let i = 0; i < 200; ++i) model.append({ content: rowText + i })
        tryCompare(output, "count", 200)
        tryCompare(output, "atBottom", true)
        wait(50)
        verify(output.atBottom)
        return output
    }

    function test_font_resize_keeps_tail_in_view() {
        const output = createTailOutput("line ")
        output.fontPixelSize = 24
        tryCompare(output, "atBottom", true)
        wait(50)
        verify(output.atBottom)
        const lastRow = findChild(output, "searchOutput_row_199")
        verify(lastRow !== null)
        verify(lastRow.y + lastRow.height <= output.contentY + output.height)
    }

    function test_narrowing_keeps_tail_in_view() {
        const output = createTailOutput("word word word word word word word word word ")
        output.width = 200
        tryCompare(output, "atBottom", true)
        wait(50)
        verify(output.atBottom)
    }

    function test_relayout_does_not_pull_a_scrolled_up_view_down() {
        const output = createTailOutput("line ")
        output.scrollToTop()
        verify(output.atTop)
        output.fontPixelSize = 24
        wait(100)
        verify(output.atTop)
        verify(!output.atBottom)
    }

    function test_select_all_selects_and_copies_the_whole_entry() {
        const model = createTemporaryObject(regressionModelComponent, testWindow.contentItem)
        verify(model !== null)
        model.append({ content: "request", entry: 0 })
        for (let i = 0; i < 200; ++i) model.append({ content: "reply " + i, entry: 1 })
        model.append({ content: "next", entry: 2 })
        const output = createTemporaryObject(outputComponent, testWindow.contentItem, {
            listModel: model,
            entryRole: "entry",
            autoScrollToBottom: true
        })
        verify(output !== null)
        tryCompare(output, "count", 202)
        tryCompare(output, "atBottom", true)
        testWindow.requestActivate()
        tryCompare(testWindow, "active", true)

        const lastReply = findChild(output, "searchOutput_content_200")
        const next = findChild(output, "searchOutput_content_201")
        verify(lastReply !== null)
        verify(next !== null)
        mouseClick(lastReply, 4, lastReply.height / 2)

        keySequence(StandardKey.SelectAll)
        compare(lastReply.selectedText, "reply 199")
        compare(findChild(output, "searchOutput_content_199").selectedText, "reply 198")
        compare(next.selectedText, "")

        Clipboard.currentText = ""
        keySequence(StandardKey.Copy)
        const expected = []
        for (let i = 0; i < 200; ++i) expected.push("reply " + i)
        compare(Clipboard.currentText, expected.join("\n"))

        mouseClick(next, 4, next.height / 2)
        compare(lastReply.selectedText, "")
        keySequence(StandardKey.SelectAll)
        compare(next.selectedText, "next")
        compare(lastReply.selectedText, "")
    }

    function createSelectionOutput(rows, properties) {
        const model = createTemporaryObject(regressionModelComponent, testWindow.contentItem)
        verify(model !== null)
        for (let i = 0; i < rows.length; ++i) model.append(rows[i])
        const props = Object.assign({ listModel: model, entryRole: "entry", height: 300 },
                                    properties || {})
        const output = createTemporaryObject(outputComponent, testWindow.contentItem, props)
        verify(output !== null)
        tryCompare(output, "count", rows.length)
        testWindow.requestActivate()
        tryCompare(testWindow, "active", true)
        return output
    }

    // Centre of the character at `pos` in row `row`, in the output's coordinates.
    function charPoint(output, row, pos) {
        const content = findChild(output, "searchOutput_content_" + row)
        verify(content !== null)
        const rect = content.positionToRectangle(pos)
        const next = content.positionToRectangle(pos + 1)
        const x = next.y === rect.y && next.x > rect.x ? (rect.x + next.x) / 2 : rect.x + 1
        return content.mapToItem(output, x, rect.y + rect.height / 2)
    }

    function threeLineRows() {
        return [
            { content: "alpha beta", entry: 0 },
            { content: "gamma delta", entry: 0 },
            { content: "epsilon zeta", entry: 1 }
        ]
    }

    function test_drag_selects_across_rows() {
        const output = createSelectionOutput(threeLineRows())
        const from = charPoint(output, 0, 6)
        const to = charPoint(output, 2, 7)
        mousePress(output, from.x - 3, from.y)
        mouseMove(output, (from.x + to.x) / 2, (from.y + to.y) / 2, -1, Qt.LeftButton)
        mouseMove(output, to.x - 3, to.y, -1, Qt.LeftButton)
        mouseRelease(output, to.x - 3, to.y)

        compare(findChild(output, "searchOutput_content_0").selectedText, "beta")
        compare(findChild(output, "searchOutput_content_1").selectedText, "gamma delta")
        compare(findChild(output, "searchOutput_content_2").selectedText, "epsilon")
        compare(output.selectedText(), "beta\ngamma delta\nepsilon")

        Clipboard.currentText = ""
        keySequence(StandardKey.Copy)
        compare(Clipboard.currentText, "beta\ngamma delta\nepsilon")
    }

    function test_drag_upwards_selects_the_same_text() {
        const output = createSelectionOutput(threeLineRows())
        const from = charPoint(output, 2, 7)
        const to = charPoint(output, 0, 6)
        mousePress(output, from.x - 3, from.y)
        mouseMove(output, to.x - 3, to.y, -1, Qt.LeftButton)
        mouseRelease(output, to.x - 3, to.y)
        compare(output.selectedText(), "beta\ngamma delta\nepsilon")
    }

    function test_click_clears_and_shift_click_extends() {
        const output = createSelectionOutput(threeLineRows())
        const start = charPoint(output, 0, 6)
        const end = charPoint(output, 1, 5)
        mouseClick(output, start.x - 3, start.y)
        verify(!output.hasSelection)
        mouseClick(output, end.x - 3, end.y, Qt.LeftButton, Qt.ShiftModifier)
        compare(output.selectedText(), "beta\ngamma")

        mouseClick(output, start.x - 3, start.y)
        verify(!output.hasSelection)
        compare(findChild(output, "searchOutput_content_1").selectedText, "")
    }

    function test_double_click_selects_a_word_and_triple_click_the_line() {
        const output = createSelectionOutput([{ content: '"hash": "00ab12", "n": 1', entry: 0 }])
        const point = charPoint(output, 0, 11)
        mouseDoubleClickSequence(output, point.x, point.y)
        compare(output.selectedText(), "00ab12")

        mouseDoubleClickSequence(output, point.x, point.y)
        mouseClick(output, point.x, point.y)
        compare(output.selectedText(), '"hash": "00ab12", "n": 1')
    }

    function test_drag_past_the_bottom_scrolls_and_keeps_selecting() {
        const rows = []
        for (let i = 0; i < 100; ++i) rows.push({ content: "line " + i, entry: i })
        const output = createSelectionOutput(rows, { autoScrollToBottom: false, height: 120 })
        output.scrollToTop()
        const from = charPoint(output, 0, 0)
        mousePress(output, from.x, from.y)
        mouseMove(output, from.x, output.height + 40, -1, Qt.LeftButton)
        tryVerify(function() { return output.contentY > 200 }, 3000)
        mouseRelease(output, from.x, output.height + 40)
        verify(output.selectedText().startsWith("line 0\nline 1\n"))
        verify(output.selectedText().split("\n").length > 10)
    }

    function test_wheel_still_scrolls_the_list() {
        const rows = []
        for (let i = 0; i < 100; ++i) rows.push({ content: "line " + i, entry: i })
        const output = createSelectionOutput(rows, { autoScrollToBottom: false, height: 120 })
        output.scrollToTop()
        const before = output.contentY
        mouseWheel(output, output.width / 2, output.height / 2, 0, -120)
        tryVerify(function() { return output.contentY > before })
    }

    function test_right_click_reports_the_row_and_keeps_a_selection_under_it() {
        const output = createSelectionOutput(threeLineRows())
        const spy = createTemporaryObject(signalSpyComponent, testWindow.contentItem, {
            target: output,
            signalName: "contextMenuRequested"
        })
        output.selectEntryAt(0)
        const inside = charPoint(output, 1, 2)
        mouseClick(output, inside.x, inside.y, Qt.RightButton)
        compare(spy.count, 1)
        compare(spy.signalArguments[0][0], 1)
        compare(output.selectedText(), "alpha beta\ngamma delta")

        const outside = charPoint(output, 2, 2)
        mouseClick(output, outside.x, outside.y, Qt.RightButton)
        compare(spy.count, 2)
        compare(spy.signalArguments[1][0], 2)
        verify(!output.hasSelection)
    }

    Component {
        id: signalSpyComponent
        SignalSpy {}
    }
}

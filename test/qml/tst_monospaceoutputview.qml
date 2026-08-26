// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.2

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
}

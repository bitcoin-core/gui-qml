// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.2

import "../../qml/controls"
import "../../qml/pages/settings"

TestCase {
    name: "SettingsDebugLogView"
    when: windowShown
    width: 900
    height: 700

    Window {
        id: testWindow
        width: 900
        height: 700
        visible: true
    }

    Component {
        id: viewComponent

        SettingsDebugLogView {
            width: 900
            height: 700
        }
    }

    function init() {
        testDebugLogModel.resetForTest(0, false)
    }

    function createView() {
        const view = createTemporaryObject(viewComponent, testWindow.contentItem)
        verify(view !== null)
        const list = findChild(view, "debugLogListView")
        verify(list !== null)
        tryCompare(list, "count", testDebugLogModel.count)
        return view
    }

    function test_uses_settings_primitives_and_neutral_card() {
        testDebugLogModel.resetForTest(3, false)
        const view = createView()

        verify(findChild(view, "debugLogSettingsHeader") !== null)
        verify(findChild(view, "debugLogPageHeading") !== null)
        const searchField = findChild(view, "debugLogSearchField")
        verify(searchField !== null)
        compare(searchField.background.border.width, 0)
        testWindow.requestActivate()
        tryCompare(testWindow, "active", true)
        searchField.forceActiveFocus()
        tryCompare(searchField, "activeFocus", true)
        compare(searchField.background.border.width, 2)
        const optionsButton = findChild(view, "debugLogOptionsButton")
        const optionsMenu = findChild(view, "debugLogOptionsMenu")
        const filterPicker = findChild(view, "debugLogMessageFilterPicker")
        const openFileButton = findChild(view, "debugLogOpenFileButton")
        verify(optionsButton !== null)
        verify(optionsMenu !== null)
        verify(filterPicker !== null)
        verify(openFileButton !== null)
        compare(optionsButton.iconSource.toString(), "image://images/ellipsis")
        compare(filterPicker.currentValue, "all")
        mouseClick(optionsButton)
        tryCompare(optionsMenu, "opened", true)
        tryVerify(function() { return filterPicker.itemAtIndex(1) !== null })
        const allMessagesOption = filterPicker.itemAtIndex(0)
        const warningsAndErrorsOption = filterPicker.itemAtIndex(1)
        compare(allMessagesOption.objectName, "debugLogFilterAllMessages")
        compare(warningsAndErrorsOption.objectName, "debugLogFilterWarningsAndErrors")
        compare(allMessagesOption.selected, true)
        compare(warningsAndErrorsOption.selected, false)
        optionsMenu.close()
        compare(openFileButton.text, "Open debug.log")
        compare(openFileButton.iconSource.toString(), "image://images/export")
        const section = findChild(view, "debugLogTableSection")
        const card = findChild(view, "debugLogTableSectionCard")
        const titles = findChild(view, "debugLogTitlesHeader")
        const footer = findChild(view, "debugLogTableFooter")
        const scrollButton = findChild(view, "debugLogScrollToBottomButton")
        const loadMoreButton = findChild(view, "debugLogLoadMoreButton")
        verify(section !== null)
        verify(card !== null)
        verify(titles !== null)
        verify(footer !== null)
        verify(scrollButton !== null)
        verify(loadMoreButton !== null)
        compare(card.color, Theme.color.neutral1)
        compare(titles.background.color, Theme.color.neutral3)
        compare(footer.color, Theme.color.neutral3)
        compare(titles.background.radius, 16)
        compare(footer.radius, 16)
        verify(findChild(view, "debugLogTitlesHeaderBottomFill") !== null)
        verify(findChild(view, "debugLogTableFooterTopFill") !== null)
        verify(scrollButton.textFontPixelSize === 13)
        verify(loadMoreButton.textFontPixelSize === 13)
    }

    function test_open_debug_log_button_invokes_model() {
        const view = createView()
        const optionsButton = findChild(view, "debugLogOptionsButton")
        const optionsMenu = findChild(view, "debugLogOptionsMenu")
        const openFileButton = findChild(view, "debugLogOpenFileButton")

        compare(testDebugLogModel.openLogFileCalls, 0)
        mouseClick(optionsButton)
        tryCompare(optionsMenu, "opened", true)
        mouseClick(openFileButton)
        compare(testDebugLogModel.openLogFileCalls, 1)
        tryCompare(optionsMenu, "opened", false)
    }

    function test_find_shortcut_focuses_search() {
        const view = createView()
        const searchField = findChild(view, "debugLogSearchField")
        const optionsButton = findChild(view, "debugLogOptionsButton")

        testWindow.requestActivate()
        tryCompare(testWindow, "active", true)
        optionsButton.forceActiveFocus()
        tryCompare(optionsButton, "activeFocus", true)
        verify(!searchField.activeFocus)

        // Qt maps ControlModifier to the Command key for standard shortcuts
        // on macOS.
        keyClick(Qt.Key_F, Qt.ControlModifier)
        tryCompare(searchField, "activeFocus", true)
    }

    function test_fixed_columns_align_and_message_grows() {
        testDebugLogModel.resetForTest(2, false)
        const view = createView()
        const list = findChild(view, "debugLogListView")
        const titles = findChild(view, "debugLogTitlesHeader")
        view.scrollToTop()
        tryVerify(function() { return list.itemAtIndex(0) !== null })
        const row = list.itemAtIndex(0)

        compare(row.typeColumnWidth, titles.typeColumnWidth)
        compare(row.timeColumnWidth, titles.timeColumnWidth)
        compare(titles.typeColumnWidth, 32)
        compare(titles.timeColumnWidth, 80)
        tryVerify(function() {
            return findChild(row, "debugLogItemRow_0Message").width > 0
        })
        compare(findChild(row, "debugLogItemRow_0Time").horizontalAlignment,
                Text.AlignLeft)
        compare(findChild(row, "debugLogItemRow_0Time").text,
                "15:42:08")
    }

    function test_type_indicators_and_alternating_rows_use_theme_colors() {
        testDebugLogModel.resetForTest(3, false)
        testDebugLogModel.setStructuredFieldsForTest(1, true,
                                                     "15:42:09")
        testDebugLogModel.setWarningForTest(2, true)
        const view = createView()
        const list = findChild(view, "debugLogListView")
        view.scrollToTop()
        tryVerify(function() { return list.itemAtIndex(2) !== null })

        const regular = list.itemAtIndex(0)
        const error = list.itemAtIndex(1)
        const warning = list.itemAtIndex(2)
        compare(findChild(regular, "debugLogItemRow_0TypeIndicator").color.a, 0)
        compare(findChild(error, "debugLogItemRow_1TypeIndicator").color,
                Theme.color.red)
        compare(findChild(warning, "debugLogItemRow_2TypeIndicator").color,
                Theme.color.amber)
        compare(regular.background.color, Theme.color.neutral1)
        compare(error.background.color, Theme.color.neutral2)
        compare(warning.background.color, Theme.color.neutral1)
    }

    function test_message_wraps_and_is_selectable() {
        testDebugLogModel.resetForTest(1, false)
        const wrapped = "A long selectable debug message. ".repeat(40)
        testDebugLogModel.setMessageForTest(0, wrapped)
        const view = createView()
        const list = findChild(view, "debugLogListView")
        view.scrollToTop()
        tryVerify(function() {
            return list.itemAtIndex(0) !== null && list.itemAtIndex(0).height > 48
        })
        const message = findChild(list.itemAtIndex(0), "debugLogItemRow_0Message")
        message.selectAll()
        compare(message.selectedText, wrapped)
    }

    function test_search_and_context_menu_filter_update_model() {
        testDebugLogModel.resetForTest(4, false)
        const view = createView()
        const search = findChild(view, "debugLogSearchField")
        const optionsButton = findChild(view, "debugLogOptionsButton")
        const optionsMenu = findChild(view, "debugLogOptionsMenu")
        const filterPicker = findChild(view, "debugLogMessageFilterPicker")

        search.text = "rpc warning"
        tryCompare(testDebugLogModel, "filter", "rpc warning")
        mouseClick(optionsButton)
        tryCompare(optionsMenu, "opened", true)
        tryVerify(function() { return filterPicker.itemAtIndex(1) !== null })
        const warningsAndErrorsOption = filterPicker.itemAtIndex(1)
        const allOption = filterPicker.itemAtIndex(0)
        mouseClick(warningsAndErrorsOption)
        compare(testDebugLogModel.warningsAndErrorsOnly, true)
        tryCompare(optionsMenu, "opened", false)
        tryCompare(optionsMenu, "visible", false)
        compare(warningsAndErrorsOption.selected, true)
        compare(allOption.selected, false)

        mouseClick(optionsButton)
        tryCompare(optionsMenu, "opened", true)
        tryVerify(function() { return filterPicker.itemAtIndex(0) !== null })
        const reopenedAllOption = filterPicker.itemAtIndex(0)
        const reopenedWarningsAndErrorsOption = filterPicker.itemAtIndex(1)
        mouseClick(reopenedAllOption)
        compare(testDebugLogModel.warningsAndErrorsOnly, false)
        tryCompare(optionsMenu, "opened", false)
        compare(reopenedAllOption.selected, true)
        compare(reopenedWarningsAndErrorsOption.selected, false)
    }

    function test_titles_stay_fixed_while_log_rows_scroll() {
        testDebugLogModel.resetForTest(100, false)
        const view = createView()
        const list = findChild(view, "debugLogListView")
        const titles = findChild(view, "debugLogTitlesHeader")
        const scrollButton = findChild(view, "debugLogScrollToBottomButton")
        const headerY = titles.mapToItem(view, 0, 0).y

        view.scrollToTop()
        tryCompare(list, "atYBeginning", true)
        compare(scrollButton.enabled, true)
        mouseClick(scrollButton)
        tryCompare(list, "atYEnd", true)
        compare(scrollButton.enabled, false)
        compare(titles.mapToItem(view, 0, 0).y, headerY)
    }

    function test_load_older_preserves_visible_anchor() {
        testDebugLogModel.resetForTest(100, true)
        const view = createView()
        const list = findChild(view, "debugLogListView")
        tryCompare(list, "atYEnd", true)
        list.positionViewAtIndex(30, ListView.Beginning)
        tryVerify(function() { return list.itemAtIndex(30) !== null })
        const anchorMessage = testDebugLogModel.messageAt(30)
        const anchorOffset = list.itemAtIndex(30).y - list.contentY

        testDebugLogModel.prependRowsForTest(3)

        tryCompare(list, "count", 103)
        compare(testDebugLogModel.messageAt(33), anchorMessage)
        tryVerify(function() {
            const shifted = list.itemAtIndex(33)
            return shifted !== null
                && Math.abs((shifted.y - list.contentY) - anchorOffset) < 0.5
        })
    }
}

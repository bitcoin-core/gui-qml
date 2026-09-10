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
        const searchBar = findChild(view, "debugLogSearchBar")
        const searchField = findChild(view, "debugLogSearchField")
        const searchIcon = findChild(view, "debugLogSearchIcon")
        const searchClearButton = findChild(view, "debugLogSearchClearButton")
        verify(searchBar !== null)
        verify(searchField !== null)
        verify(searchIcon !== null)
        verify(searchClearButton !== null)
        compare(searchBar.height, 40)
        compare(searchField.height, 40)
        compare(searchBar.background.visible, false)
        compare(searchField.background.color, Theme.color.neutral2)
        compare(searchIcon.source.toString(), "image://images/search")
        compare(searchField.background.border.width, 0)
        testWindow.requestActivate()
        tryCompare(testWindow, "active", true)
        searchField.forceActiveFocus()
        tryCompare(searchField, "activeFocus", true)
        compare(searchField.background.border.width, 0)
        searchField.text = "rpc"
        compare(searchClearButton.visible, true)
        mouseClick(searchClearButton)
        compare(searchField.text, "")
        const filterButton = findChild(view, "debugLogFilterButton")
        const inactiveFilterIcon = findChild(view, "debugLogFilterButtonInactiveIcon")
        const activeFilterIcon = findChild(view, "debugLogFilterButtonActiveIcon")
        const filterMenu = findChild(view, "debugLogFilterMenu")
        const filterPicker = findChild(view, "debugLogMessageFilterPicker")
        const optionsButton = findChild(view, "debugLogOptionsButton")
        const optionsMenu = findChild(view, "debugLogOptionsMenu")
        const openFileButton = findChild(view, "debugLogOpenFileButton")
        verify(filterButton !== null)
        verify(inactiveFilterIcon !== null)
        verify(activeFilterIcon !== null)
        verify(filterMenu !== null)
        verify(optionsButton !== null)
        verify(optionsMenu !== null)
        verify(filterPicker !== null)
        verify(openFileButton !== null)
        compare(filterButton.active, false)
        compare(filterButton.background.color, Theme.color.neutral1)
        compare(filterButton.iconSize, 24)
        verify(filterButton.x < optionsButton.x)
        compare(filterButton.x, searchBar.x + searchBar.width + 16)
        compare(inactiveFilterIcon.source.toString(), "qrc:/icons/filter")
        compare(inactiveFilterIcon.color, Theme.color.neutral6)
        compare(inactiveFilterIcon.opacity, 1)
        compare(activeFilterIcon.source.toString(), "qrc:/icons/filter-active")
        compare(activeFilterIcon.opacity, 0)
        compare(optionsButton.iconSource.toString(), "image://images/ellipsis")
        compare(optionsButton.iconSize, 30)
        compare(optionsButton.iconItem.width, 30)
        compare(optionsButton.iconItem.height, 30)
        compare(optionsButton.background.color, Theme.color.neutral1)
        compare(filterPicker.currentValue, "all")
        mouseClick(filterButton)
        tryCompare(filterMenu, "opened", true)
        tryVerify(function() { return filterPicker.itemAtIndex(1) !== null })
        const allMessagesOption = filterPicker.itemAtIndex(0)
        const warningsAndErrorsOption = filterPicker.itemAtIndex(1)
        compare(allMessagesOption.objectName, "debugLogFilterAllMessages")
        compare(warningsAndErrorsOption.objectName, "debugLogFilterWarningsAndErrors")
        compare(allMessagesOption.selected, true)
        compare(warningsAndErrorsOption.selected, false)
        filterMenu.close()
        verify(findChild(view, "debugLogOptionsDivider") === null)
        mouseClick(optionsButton)
        tryCompare(optionsMenu, "opened", true)
        compare(openFileButton.text, "Open debug.log")
        compare(openFileButton.iconSource.toString(), "image://images/export")
        optionsMenu.close()
        const section = findChild(view, "debugLogTableSection")
        const card = findChild(view, "debugLogTableSectionCard")
        const titles = findChild(view, "debugLogTitlesHeader")
        const titlesDivider = findChild(view, "debugLogTitlesHeaderDivider")
        const footer = findChild(view, "debugLogTableFooter")
        const footerDivider = findChild(view, "debugLogTableFooterDivider")
        const scrollButton = findChild(view, "debugLogScrollToBottomButton")
        const loadMoreButton = findChild(view, "debugLogLoadMoreButton")
        verify(section !== null)
        verify(card !== null)
        verify(titles !== null)
        verify(titlesDivider !== null)
        verify(footer !== null)
        verify(footerDivider !== null)
        verify(scrollButton !== null)
        verify(loadMoreButton !== null)
        compare(card.color, Theme.color.neutral1)
        compare(titles.background.color, Theme.color.neutral1)
        compare(footer.color, Theme.color.neutral1)
        compare(titlesDivider.height, 1)
        compare(titlesDivider.color, Theme.color.neutral2)
        compare(footerDivider.height, 1)
        compare(footerDivider.color, Theme.color.neutral2)
        compare(titles.background.radius, 16)
        compare(footer.radius, 16)
        verify(findChild(view, "debugLogTitlesHeaderBottomFill") !== null)
        verify(findChild(view, "debugLogTableFooterTopFill") !== null)
        verify(scrollButton.textFontPixelSize === 13)
        verify(loadMoreButton.textFontPixelSize === 13)
        compare(scrollButton.bold, true)
        compare(loadMoreButton.bold, true)
        compare(scrollButton.background.border.color, Theme.color.neutral2)
        compare(loadMoreButton.background.border.color, Theme.color.neutral2)
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

    function test_toolbar_icons_keep_their_size_and_use_hover_tint() {
        const view = createView()
        const filterButton = findChild(view, "debugLogFilterButton")
        const filterIcon = findChild(view, "debugLogFilterButtonInactiveIcon")
        const optionsButton = findChild(view, "debugLogOptionsButton")
        const optionsIcon = findChild(view, "debugLogOptionsButtonIcon")

        compare(filterIcon.width, 24)
        compare(filterIcon.height, 24)
        compare(optionsIcon.width, 30)
        compare(optionsIcon.height, 30)

        mouseMove(filterButton, filterButton.width / 2, filterButton.height / 2)
        tryCompare(filterButton, "hovered", true)
        tryCompare(filterIcon, "color", Theme.color.orange)
        compare(filterIcon.width, 24)
        compare(filterIcon.height, 24)

        mouseMove(optionsButton, optionsButton.width / 2, optionsButton.height / 2)
        tryCompare(optionsButton, "hovered", true)
        tryCompare(optionsIcon, "color", Theme.color.orange)
        compare(optionsIcon.width, 30)
        compare(optionsIcon.height, 30)
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

    function test_search_and_filter_menu_update_model_and_button_state() {
        testDebugLogModel.resetForTest(4, false)
        const view = createView()
        const search = findChild(view, "debugLogSearchField")
        const filterButton = findChild(view, "debugLogFilterButton")
        const inactiveFilterIcon = findChild(view, "debugLogFilterButtonInactiveIcon")
        const activeFilterIcon = findChild(view, "debugLogFilterButtonActiveIcon")
        const filterMenu = findChild(view, "debugLogFilterMenu")
        const filterPicker = findChild(view, "debugLogMessageFilterPicker")

        search.text = "rpc warning"
        tryCompare(testDebugLogModel, "filter", "rpc warning")
        mouseClick(filterButton)
        tryCompare(filterMenu, "opened", true)
        tryVerify(function() { return filterPicker.itemAtIndex(1) !== null })
        const warningsAndErrorsOption = filterPicker.itemAtIndex(1)
        const allOption = filterPicker.itemAtIndex(0)
        mouseClick(warningsAndErrorsOption)
        compare(testDebugLogModel.warningsAndErrorsOnly, true)
        tryCompare(filterMenu, "opened", false)
        tryCompare(filterMenu, "visible", false)
        compare(filterButton.active, true)
        tryCompare(inactiveFilterIcon, "opacity", 0)
        tryCompare(inactiveFilterIcon, "scale", filterButton.minimizedIconScale)
        tryCompare(activeFilterIcon, "opacity", 1)
        tryCompare(activeFilterIcon, "scale", 1)
        compare(activeFilterIcon.color, Theme.color.orange)
        compare(warningsAndErrorsOption.selected, true)
        compare(allOption.selected, false)

        mouseClick(filterButton)
        tryCompare(filterMenu, "opened", true)
        tryVerify(function() { return filterPicker.itemAtIndex(0) !== null })
        const reopenedAllOption = filterPicker.itemAtIndex(0)
        const reopenedWarningsAndErrorsOption = filterPicker.itemAtIndex(1)
        mouseClick(reopenedAllOption)
        compare(testDebugLogModel.warningsAndErrorsOnly, false)
        tryCompare(filterMenu, "opened", false)
        compare(filterButton.active, false)
        tryCompare(inactiveFilterIcon, "opacity", 1)
        tryCompare(inactiveFilterIcon, "scale", 1)
        tryCompare(activeFilterIcon, "opacity", 0)
        tryCompare(activeFilterIcon, "scale", filterButton.minimizedIconScale)
        compare(inactiveFilterIcon.color, Theme.color.neutral6)
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

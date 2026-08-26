// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.2
import org.bitcoincore.qt 1.0

import "../../qml/components"
import "../../qml/controls"

TestCase {
    name: "SearchBar"
    when: windowShown
    width: 520
    height: 180

    Window {
        id: testWindow
        width: 520
        height: 180
        visible: true
    }

    Component {
        id: searchBarComponent

        SearchBar {
            objectName: "sharedSearchBar"
            fieldObjectName: "sharedSearchField"
            searchIconObjectName: "sharedSearchIcon"
            clearButtonObjectName: "sharedSearchClearButton"
            navigationControlObjectName: "sharedSearchNavigation"
            previousButtonObjectName: "sharedSearchPreviousButton"
            nextButtonObjectName: "sharedSearchNextButton"
            placeholderText: "Find output"
        }
    }

    function createSearchBar() {
        const searchBar = createTemporaryObject(searchBarComponent,
                                                testWindow.contentItem)
        verify(searchBar !== null)
        return searchBar
    }

    function test_shared_surface_and_clear_button() {
        const searchBar = createSearchBar()
        const field = findChild(searchBar, "sharedSearchField")
        const searchIcon = findChild(searchBar, "sharedSearchIcon")
        const clearButton = findChild(searchBar, "sharedSearchClearButton")
        verify(field !== null)
        verify(searchIcon !== null)
        verify(clearButton !== null)
        compare(searchBar.height, 40)
        compare(searchBar.background.visible, false)
        compare(searchBar.padding, 0)
        compare(searchBar.background.radius, 8)
        compare(field.background.color, Theme.color.neutral2)
        compare(field.background.radius, 5)
        compare(field.placeholderText, "Find output")
        compare(searchIcon.source.toString(), "image://images/search")
        compare(searchIcon.size, 14)
        verify(searchIcon.x < field.leftPadding)
        compare(clearButton.visible, false)

        searchBar.text = "needle"
        compare(field.text, "needle")
        compare(clearButton.visible, true)
        compare(clearButton.width, 14)
        compare(clearButton.height, 14)
        compare(clearButton.contentItem.size, 6)
        compare(clearButton.contentItem.strokeWidth, 1.5)
        compare(clearButton.background.border.color, Theme.color.neutral4)
        compare(clearButton.contentItem.strokeColor, Theme.color.neutral4)
        compare(clearButton.background.radius, clearButton.width / 2)
        mouseClick(clearButton)
        compare(searchBar.text, "")
        compare(clearButton.visible, false)
    }

    function test_optional_navigation_buttons() {
        const searchBar = createSearchBar()
        const navigation = findChild(searchBar, "sharedSearchNavigation")
        const previousButton = findChild(searchBar, "sharedSearchPreviousButton")
        const nextButton = findChild(searchBar, "sharedSearchNextButton")
        const previousFocusBorder = findChild(searchBar, "sharedSearchPreviousButtonFocusBorder")
        const nextFocusBorder = findChild(searchBar, "sharedSearchNextButtonFocusBorder")
        const previousIcon = findChild(searchBar, "sharedSearchPreviousButtonIcon")
        const nextIcon = findChild(searchBar, "sharedSearchNextButtonIcon")
        verify(navigation !== null)
        verify(previousButton !== null)
        verify(nextButton !== null)
        verify(previousFocusBorder !== null)
        verify(nextFocusBorder !== null)
        verify(previousIcon !== null)
        verify(nextIcon !== null)
        compare(previousButton.visible, false)
        compare(nextButton.visible, false)
        compare(navigation.visible, false)

        searchBar.showNavigationButtons = true
        searchBar.width = 140
        compare(searchBar.height, 48)
        compare(searchBar.inputField.height, 40)
        compare(searchBar.background.visible, true)
        compare(searchBar.background.color, Theme.color.neutral1)
        compare(searchBar.padding, 4)
        compare(navigation.visible, true)
        compare(navigation.width, 54)
        searchBar.navigationEnabled = false
        compare(previousButton.visible, true)
        compare(nextButton.visible, true)
        compare(previousButton.width, 26)
        compare(nextButton.width, 26)
        compare(previousButton.enabled, false)
        compare(nextButton.enabled, false)
        compare(previousIcon.width, 14)
        compare(previousIcon.height, 14)
        compare(nextIcon.width, 14)
        compare(nextIcon.height, 14)
        compare(previousIcon.strokeWidth, 2)
        compare(nextIcon.strokeWidth, 2)
        compare(previousIcon.strokeColor, Theme.color.neutral4)
        compare(nextIcon.strokeColor, Theme.color.neutral4)
        compare(previousIcon.rotation, -90)
        compare(nextIcon.rotation, 90)

        searchBar.text = "a long search term that must not compress navigation"
        searchBar.navigationEnabled = true
        compare(previousButton.width, 26)
        compare(nextButton.width, 26)
        compare(previousIcon.width, 14)
        compare(previousIcon.height, 14)
        compare(nextIcon.width, 14)
        compare(nextIcon.height, 14)
        compare(previousIcon.strokeColor, Theme.color.neutral8)
        compare(nextIcon.strokeColor, Theme.color.neutral8)
        compare(previousButton.enabled, true)
        compare(nextButton.enabled, true)

        testWindow.requestActivate()
        tryCompare(testWindow, "active", true)
        searchBar.inputField.forceActiveFocus()
        tryCompare(searchBar.inputField, "activeFocus", true)
        keyClick(Qt.Key_Tab)
        tryCompare(previousButton, "activeFocus", true)
        tryCompare(previousFocusBorder, "visible", true)
        compare(previousFocusBorder.border.color, Theme.color.purple)
        compare(nextFocusBorder.visible, false)
        keyClick(Qt.Key_Tab)
        tryCompare(nextButton, "activeFocus", true)
        tryCompare(nextFocusBorder, "visible", true)
        compare(nextFocusBorder.border.color, Theme.color.purple)
        compare(previousFocusBorder.visible, false)

        // Restore the normal test width before pointer interaction. The
        // constrained width above exists only to exercise layout compression.
        searchBar.width = searchBar.implicitWidth
        wait(0)
        const previousSpy = signalSpy.createObject(searchBar, {
            target: searchBar,
            signalName: "previousRequested"
        })
        const nextSpy = signalSpy.createObject(searchBar, {
            target: searchBar,
            signalName: "nextRequested"
        })
        verify(previousSpy.valid)
        verify(nextSpy.valid)
        previousButton.clicked()
        nextButton.clicked()
        compare(previousSpy.count, 1)
        compare(nextSpy.count, 1)
    }

    Component {
        id: signalSpy
        SignalSpy {}
    }
}

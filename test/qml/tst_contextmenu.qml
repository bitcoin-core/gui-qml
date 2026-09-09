// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtTest 1.2
import "../../qml/controls"

TestCase {
    name: "ContextMenu"
    when: windowShown
    width: 400
    height: 400

    Item {
        id: host
        width: parent.width
        height: parent.height
    }

    Component {
        id: emptyMenuComponent

        ContextMenu {
            x: 20
            y: 20
        }
    }

    Component {
        id: menuWithButtonComponent

        ContextMenu {
            id: testMenu
            x: 20
            y: 20

            property int triggerCount: 0
            property bool openedWhenTriggered: true
            property alias button: testButton

            ContextMenuButton {
                id: testButton
                text: "Action"
                onTriggered: {
                    testMenu.openedWhenTriggered = testMenu.opened
                    testMenu.triggerCount += 1
                }
            }
        }
    }

    Component {
        id: menuWithPersistentItemsComponent

        ContextMenu {
            id: testMenu
            x: 20
            y: 20

            property alias button: testButton
            property alias toggle: testToggle

            ContextMenuButton {
                id: testButton
                text: "Keep open"
                autoClose: false
            }

            ContextMenuToggle {
                id: testToggle
                text: "Toggle"
            }
        }
    }

    function openMenu(component) {
        const menu = createTemporaryObject(component, host)
        verify(menu !== null)
        menu.open()
        tryCompare(menu, "opened", true)
        wait(0)
        return menu
    }

    function test_empty_menu_clamps_to_min_width() {
        const menu = openMenu(emptyMenuComponent)
        compare(menu.implicitWidth, menu.minMenuWidth)
        compare(menu.background.color, Theme.color.neutral1)
    }

    function test_escape_closes_focused_menu() {
        const menu = openMenu(emptyMenuComponent)
        tryCompare(menu, "activeFocus", true)

        keyClick(Qt.Key_Escape)
        tryCompare(menu, "opened", false)
    }

    function test_button_closes_menu_by_default() {
        const menu = openMenu(menuWithButtonComponent)

        mouseClick(menu.button, menu.button.width / 2, menu.button.height / 2)
        compare(menu.triggerCount, 1)
        compare(menu.openedWhenTriggered, false)
        tryCompare(menu, "opened", false)
    }

    function test_button_can_opt_out_of_closing_menu() {
        const menu = openMenu(menuWithPersistentItemsComponent)

        mouseClick(menu.button, menu.button.width / 2, menu.button.height / 2)
        compare(menu.opened, true)
    }

    function test_disabled_button_does_not_trigger_or_close_menu() {
        const menu = openMenu(menuWithButtonComponent)
        menu.button.enabled = false

        mouseClick(menu.button, menu.button.width / 2, menu.button.height / 2)
        compare(menu.triggerCount, 0)
        compare(menu.opened, true)
    }

    function test_toggle_stays_open() {
        const menu = openMenu(menuWithPersistentItemsComponent)

        mouseClick(menu.toggle, menu.toggle.width / 2, menu.toggle.height / 2)
        compare(menu.toggle.checked, true)
        compare(menu.opened, true)
    }

    function test_keyboard_activation_triggers_and_closes_menu() {
        const menu = openMenu(menuWithButtonComponent)
        menu.button.forceActiveFocus()
        verify(menu.button.activeFocus)

        keyClick(Qt.Key_Space)
        compare(menu.triggerCount, 1)
        tryCompare(menu, "opened", false)
    }
}

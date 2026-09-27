// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtTest 1.2
import "../../qml/controls"

TestCase {
    name: "DropdownButton"
    when: windowShown
    width: 400
    height: 200

    Item {
        id: host
        width: parent.width
        height: parent.height
    }

    Component {
        id: plainComponent

        DropdownButton {
            objectName: "plainDropdownButton"
            width: 200
            height: implicitHeight
            text: "All dates"
            caretSource: "qrc:/icons/caret-down-medium-filled"
        }
    }

    Component {
        id: popupComponent

        Popup {
            width: 240
            height: 60
            padding: 10
            property alias button: _button

            contentItem: DropdownButton {
                id: _button
                width: 200
                height: implicitHeight
                text: "All dates"
                caretSource: "qrc:/icons/caret-down-medium-filled"
            }
        }
    }

    function openPopup() {
        const popup = createTemporaryObject(popupComponent, host)
        verify(popup !== null)
        popup.open()
        tryCompare(popup, "opened", true)
        return popup
    }

    function child(button, objectName) {
        const item = findChild(button, objectName)
        verify(item !== null)
        return item
    }

    function test_defaults() {
        const button = createTemporaryObject(plainComponent, host)
        verify(button !== null)
        compare(button.implicitHeight, 30)
        compare(button.caretSize, 20)
        compare(button.opened, false)
        compare(button.focusPolicy, Qt.StrongFocus)
        compare(button.Accessible.name, button.text)
        compare(child(button, "dropdownButtonLabel").lineHeight, Theme.text.description.lineHeight)
        compare(child(button, "dropdownButtonLabel").lineHeightMode, Text.FixedHeight)
        verify(button.hoverEnabled)
    }

    function test_pointer_and_keyboard_activation_emit_clicked() {
        const popup = openPopup()
        const button = popup.button
        let count = 0
        button.clicked.connect(function() { count += 1 })

        mouseClick(button, button.width / 2, button.height / 2)
        compare(count, 1)

        button.forceActiveFocus()
        verify(button.activeFocus)
        keyClick(Qt.Key_Space)
        compare(count, 2)
    }

    function test_subtitle_visibility_tracks_content() {
        const popup = openPopup()
        const button = popup.button
        const subtitleItem = child(button, "dropdownButtonSubtitle")

        compare(subtitleItem.visible, false)
        button.subtitle = "now visible"
        tryCompare(subtitleItem, "visible", true)
        button.subtitle = ""
        tryCompare(subtitleItem, "visible", false)
    }

    function test_opened_state_changes_background_and_animates_caret() {
        const button = createTemporaryObject(plainComponent, host)
        verify(button !== null)
        const caretItem = child(button, "dropdownButtonCaret")

        button.opened = false
        compare(button.background.color, button.defaultBgColor)
        compare(caretItem.rotation, 0)

        button.opened = true
        compare(button.background.color, button.hoverBgColor)
        tryCompare(caretItem, "rotation", 180)

        button.opened = false
        tryCompare(caretItem, "rotation", 0)
    }

    function test_caret_size_controls_both_dimensions() {
        const button = createTemporaryObject(plainComponent, host)
        verify(button !== null)
        const caretItem = child(button, "dropdownButtonCaret")

        button.caretSize = 30
        compare(caretItem.width, 30)
        compare(caretItem.height, 30)
        tryCompare(button, "implicitHeight", 34)
    }

    function test_disabled_button_does_not_activate_and_keeps_neutral_caret() {
        const popup = openPopup()
        const button = popup.button
        const caretItem = child(button, "dropdownButtonCaret")
        let count = 0
        button.clicked.connect(function() { count += 1 })

        button.enabled = false
        mouseClick(button, button.width / 2, button.height / 2)
        compare(count, 0)
        compare(caretItem.color, Theme.color.neutral4)
    }

    function test_keyboard_focus_has_visible_state() {
        const button = createTemporaryObject(plainComponent, host)
        verify(button !== null)
        child(button, "dropdownButtonFocusBorder")

        button.forceActiveFocus(Qt.TabFocusReason)
        verify(button.visualFocus)
        compare(button.background.color, button.hoverBgColor)
    }
}

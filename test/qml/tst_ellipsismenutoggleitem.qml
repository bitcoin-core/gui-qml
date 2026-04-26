// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtTest 1.2
import "../../qml/controls"

TestCase {
    name: "EllipsisMenuToggleItem"
    when: windowShown
    width: 400
    height: 200

    Item {
        id: host
        width: parent.width
        height: parent.height
    }

    Component {
        id: toggleItemComponent

        EllipsisMenuToggleItem {
            width: 280
            text: "Multiple Recipients"
        }
    }

    function test_toggle_defaults_unchecked() {
        const item = createTemporaryObject(toggleItemComponent, host)
        verify(item !== null)

        compare(item.implicitWidth, 280)
        compare(item.implicitHeight, 48)
        compare(item.checked, false)
    }

    function test_external_checked_assignment_is_stable() {
        const item = createTemporaryObject(toggleItemComponent, host)
        verify(item !== null)

        item.checked = true
        compare(item.checked, true)

        item.checked = false
        compare(item.checked, false)
    }

    function test_toggle_item_exposes_button_contract() {
        const item = createTemporaryObject(toggleItemComponent, host)
        verify(item !== null)

        verify(item.enabled)
        verify(item.hoverEnabled)
        verify(item.checkable)
        compare(item.padding, 10)
    }
}

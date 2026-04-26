// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtTest 1.2
import "../../qml/controls"

TestCase {
    name: "EllipsisMenuActionItem"
    when: windowShown
    width: 400
    height: 200

    Item {
        id: host
        width: parent.width
        height: parent.height
    }

    Component {
        id: actionItemComponent

        EllipsisMenuActionItem {
            width: 280
            text: "Import PSBT from file…"
            leftIconSource: "qrc:/icons/file"
        }
    }

    function test_action_item_defaults() {
        const item = createTemporaryObject(actionItemComponent, host)
        verify(item !== null)

        compare(item.implicitWidth, 280)
        compare(item.implicitHeight, 33)
        compare(item.text, "Import PSBT from file…")
        verify(item.hoverEnabled)
        verify(item.leftIconSource.toString().indexOf("file") !== -1)
    }

    function test_action_item_exposes_button_contract() {
        const item = createTemporaryObject(actionItemComponent, host)
        verify(item !== null)

        verify(item.enabled)
        verify(item.hoverEnabled)
        compare(item.padding, 0)
    }
}

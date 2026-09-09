// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtTest 1.2
import "../../qml/controls"

TestCase {
    name: "ContextMenuButton"
    when: windowShown
    width: 400
    height: 200

    Item {
        id: host
        width: parent.width
        height: parent.height
    }

    Component {
        id: normalComponent

        ContextMenuButton {
            width: 280
            height: implicitHeight
            text: "Duplicate"
            iconSource: Qt.resolvedUrl("../../qml/res/icons/file.png")
        }
    }

    Component {
        id: destructiveComponent

        ContextMenuButton {
            width: 280
            height: implicitHeight
            text: "Delete"
            role: ContextMenuButton.Destructive
        }
    }

    function test_defaults() {
        const button = createTemporaryObject(normalComponent, host)
        verify(button !== null)
        compare(button.implicitHeight, 36)
        compare(button.role, ContextMenuButton.Normal)
        compare(button.autoClose, true)
        compare(button.focusPolicy, Qt.StrongFocus)
        compare(button.hoverBackgroundColor, Theme.color.neutral3)
        verify(button.hoverEnabled)

        button.forceActiveFocus(Qt.TabFocusReason)
        tryCompare(button, "visualFocus", true)
        compare(button.background.color, Theme.color.neutral3)
    }

    function test_destructive_role_marker() {
        const button = createTemporaryObject(destructiveComponent, host)
        verify(button !== null)
        compare(button.role, ContextMenuButton.Destructive)
    }
}

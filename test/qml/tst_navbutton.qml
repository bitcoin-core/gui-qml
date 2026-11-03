// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtTest 1.2
import "../../qml/controls"

TestCase {
    name: "NavButton"
    when: windowShown
    visible: true
    width: 300
    height: 200

    Component {
        id: iconTextButtonComponent

        NavButton {
            iconSource: "image://images/caret-left"
            text: "Back"
        }
    }

    function test_contentItemRespectsPadding() {
        const button = createTemporaryObject(iconTextButtonComponent, this)
        verify(button !== null)

        compare(button.contentItem.x, button.leftPadding)
        compare(button.contentItem.y, button.topPadding)
        compare(button.contentItem.width, button.width - button.leftPadding - button.rightPadding)
        compare(button.contentItem.height, button.height - button.topPadding - button.bottomPadding)
    }

    function test_releaseOutsideDoesNotClick() {
        const button = createTemporaryObject(iconTextButtonComponent, this)
        verify(button !== null)
        let clicks = 0
        button.clicked.connect(function() { ++clicks })

        mousePress(button)
        verify(button.down)
        mouseMove(button, button.width + 40, button.height / 2)
        mouseRelease(button, button.width + 40, button.height / 2)
        compare(clicks, 0)
        verify(!button.down)

        mouseClick(button)
        compare(clicks, 1)
    }
}

// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtTest 1.2
import "../../qml/controls"

TestCase {
    function init() {
        testSetup.failOnWarnings()
    }

    name: "ContextMenuToggle"
    when: windowShown
    width: 400
    height: 200

    Item {
        id: host
        width: parent.width
        height: parent.height
    }

    Component {
        id: toggleComponent

        ContextMenuToggle {
            width: 280
            height: implicitHeight
            text: "Favorite"
        }
    }

    function test_defaults_unchecked() {
        const toggle = createTemporaryObject(toggleComponent, host)
        verify(toggle !== null)
        compare(toggle.implicitHeight, 36)
        compare(toggle.checked, false)
        compare(toggle.autoClose, false)
        verify(toggle.checkable)
    }

    function test_external_assignment_propagates() {
        const toggle = createTemporaryObject(toggleComponent, host)
        verify(toggle !== null)
        toggle.checked = true
        compare(toggle.checked, true)
        toggle.checked = false
        compare(toggle.checked, false)
    }
}

// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtTest 1.2
import org.bitcoincore.qt 1.0
import "../../qml/controls"

TestCase {
    name: "CoreCheckBox"
    when: windowShown
    width: 400
    height: 200

    property bool initialIsDesktop: true

    Component {
        id: checkBoxComponent
        CoreCheckBox {}
    }

    function initTestCase() {
        // AppMode is a process-wide singleton shared with the other test files.
        initialIsDesktop = AppMode.isDesktop
    }

    function cleanupTestCase() {
        AppMode.isDesktop = initialIsDesktop
    }

    function test_hover_enabled_follows_app_mode() {
        const box = createTemporaryObject(checkBoxComponent, this)
        verify(box !== null)

        AppMode.isDesktop = true
        compare(box.hoverEnabled, true)

        AppMode.isDesktop = false
        compare(box.hoverEnabled, false)

        AppMode.isDesktop = true
        compare(box.hoverEnabled, true)
    }

    function test_toggles_and_reflects_checked_state() {
        const box = createTemporaryObject(checkBoxComponent, this)
        verify(box !== null)
        verify(box.checkable)
        verify(!box.checked)
        compare(box.contentItem.color, "#00000000")
        compare(box.contentItem.border.color, box.borderColor)

        box.toggle()

        verify(box.checked)
        compare(box.contentItem.color, box.fillColor)
        compare(box.contentItem.border.color, box.fillColor)

        box.toggle()

        verify(!box.checked)
        compare(box.contentItem.color, "#00000000")
        compare(box.contentItem.border.color, box.borderColor)
    }
}

// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15 as Controls
import QtTest 1.2
import "../../qml/controls"

TestCase {
    id: testCase
    name: "ButtonKeyActivation"
    when: windowShown
    visible: true
    width: 320
    height: 320

    property int clicks: 0

    Column {
        Controls.Button { id: button; text: "Button"; onClicked: testCase.clicks += 1 }
        NeutralButton { id: neutral; text: "Neutral"; onClicked: testCase.clicks += 1 }
        CheckBox { id: checkBox; text: "Check" }
        OptionSwitch { id: optionSwitch }
    }

    function init() {
        clicks = 0
        checkBox.checked = false
        optionSwitch.checked = false
    }

    function test_buttons_activate_on_return_enter_and_space() {
        for (const target of [button, neutral]) {
            target.forceActiveFocus(Qt.TabFocusReason)
            const before = clicks
            keyClick(Qt.Key_Return)
            keyClick(Qt.Key_Enter)
            keyClick(Qt.Key_Space)
            compare(clicks, before + 3, target.text)
        }
    }

    function test_toggles_keep_space_only() {
        for (const toggle of [checkBox, optionSwitch]) {
            toggle.forceActiveFocus(Qt.TabFocusReason)
            keyClick(Qt.Key_Return)
            compare(toggle.checked, false)
            keyClick(Qt.Key_Space)
            compare(toggle.checked, true)
        }
    }
}

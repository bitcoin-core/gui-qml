// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtTest 1.2
import "../../qml/controls"

TestCase {
    name: "WalletTypeListItem"
    when: windowShown
    width: 500
    height: 300

    Component {
        id: listItemComponent
        WalletTypeListItem {
            width: 450
            height: 60
            title: "Regular"
            description: "Fully managed in this application."
            iconSource: "image://images/singlesig-wallet"
        }
    }

    function test_initial_properties() {
        const item = createTemporaryObject(listItemComponent, this)
        verify(item !== null)
        compare(item.title, "Regular")
        compare(item.description, "Fully managed in this application.")
        compare(item.iconSource, "image://images/singlesig-wallet")
        verify(item.enabled)
    }

    function test_enabled_opacity() {
        const item = createTemporaryObject(listItemComponent, this)
        verify(item !== null)
        compare(item.opacity, 1.0)
    }

    function test_disabled_opacity() {
        const item = createTemporaryObject(listItemComponent, this)
        verify(item !== null)
        item.enabled = false
        compare(item.opacity, 0.4)
    }

    function test_click_emits_signal() {
        const item = createTemporaryObject(listItemComponent, this)
        verify(item !== null)

        var clicked = false
        item.clicked.connect(function() { clicked = true })
        item.clicked()
        verify(clicked)
    }

    function test_disabled_blocks_click() {
        const item = createTemporaryObject(listItemComponent, this)
        verify(item !== null)
        item.enabled = false

        var clicked = false
        item.clicked.connect(function() { clicked = true })
        mouseClick(item, item.width / 2, item.height / 2)
        verify(!clicked)
    }
}

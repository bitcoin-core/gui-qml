// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtTest 1.2
import "../../qml/pages/wallet"

TestCase {
    name: "WatchOnlyXpub"
    when: windowShown
    width: 500
    height: 600

    // Valid BIP32 xpub from Bitcoin Core test vectors
    readonly property string validXpub:
        "xpub661MyMwAqRbcFtXgS5sYJABqqG9YLmC4Q1Rdap9gSE8NqtwybGhePY2gZ29ESFjqJoCu1Rupje8YtGqsefD265TMg7usUDFdp6W1EGMcet8"

    Component {
        id: xpubPageComponent
        WatchOnlyXpub {}
    }

    function findChild(parent, objectName) {
        if (parent.objectName === objectName) return parent
        for (var i = 0; i < parent.children.length; i++) {
            var result = findChild(parent.children[i], objectName)
            if (result) return result
        }
        if (parent.contentItem) {
            var result = findChild(parent.contentItem, objectName)
            if (result) return result
        }
        return null
    }

    function test_next_button_disabled_initially() {
        const page = createTemporaryObject(xpubPageComponent, this)
        verify(page !== null)

        var btn = findChild(page, "watchOnlyXpubNextButton")
        verify(btn !== null)
        verify(!btn.enabled)
    }

    function test_invalid_input_keeps_button_disabled() {
        const page = createTemporaryObject(xpubPageComponent, this)
        verify(page !== null)

        var input = findChild(page, "watchOnlyXpubInput")
        verify(input !== null)
        input.text = "not-a-valid-xpub"

        var btn = findChild(page, "watchOnlyXpubNextButton")
        verify(btn !== null)
        verify(!btn.enabled)
    }

    function test_valid_xpub_enables_button() {
        const page = createTemporaryObject(xpubPageComponent, this)
        verify(page !== null)

        var input = findChild(page, "watchOnlyXpubInput")
        verify(input !== null)
        input.text = validXpub

        var btn = findChild(page, "watchOnlyXpubNextButton")
        verify(btn !== null)
        tryVerify(function() { return btn.enabled }, 2000)
    }

    function test_next_signal_sets_xpub() {
        const page = createTemporaryObject(xpubPageComponent, this)
        verify(page !== null)

        var input = findChild(page, "watchOnlyXpubInput")
        verify(input !== null)
        input.text = validXpub

        var btn = findChild(page, "watchOnlyXpubNextButton")
        verify(btn !== null)
        tryVerify(function() { return btn.enabled }, 2000)

        var emitted = false
        page.next.connect(function() { emitted = true })
        btn.clicked()
        verify(emitted)
        compare(page.xpub, validXpub)
    }
}

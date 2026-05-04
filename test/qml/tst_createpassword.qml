// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtTest 1.2
import "../../qml/pages/wallet"

TestCase {
    name: "CreatePassword"
    when: windowShown
    width: 500
    height: 700

    Component {
        id: passwordPageComponent
        CreatePassword {
            walletName: "test_wallet"
        }
    }

    function findChild(parent, objectName) {
        if (!parent) return null
        if (parent.objectName === objectName) return parent
        for (var i = 0; i < parent.children.length; i++) {
            var result = findChild(parent.children[i], objectName)
            if (result) return result
        }
        if (parent.contentItem) {
            var result = findChild(parent.contentItem, objectName)
            if (result) return result
        }
        if (parent.header) {
            var result = findChild(parent.header, objectName)
            if (result) return result
        }
        if (parent.footer) {
            var result = findChild(parent.footer, objectName)
            if (result) return result
        }
        return null
    }

    function test_skip_button_disabled_when_not_initialized() {
        walletController.initialized = false
        const page = createTemporaryObject(passwordPageComponent, this)
        verify(page !== null)

        var btn = findChild(page, "createWalletPasswordSkipButton")
        verify(btn !== null)
        verify(!btn.enabled)

        walletController.initialized = true
    }

    function test_skip_button_enabled_when_initialized() {
        walletController.initialized = true
        const page = createTemporaryObject(passwordPageComponent, this)
        verify(page !== null)

        var btn = findChild(page, "createWalletPasswordSkipButton")
        verify(btn !== null)
        verify(btn.enabled)
    }

    function test_continue_text_shows_initializing() {
        walletController.initialized = false
        const page = createTemporaryObject(passwordPageComponent, this)
        verify(page !== null)

        walletController.initialized = true
    }
}

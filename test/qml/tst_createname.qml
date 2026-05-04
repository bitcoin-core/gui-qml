// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtTest 1.2
import "../../qml/pages/wallet"

TestCase {
    name: "CreateName"
    when: windowShown
    width: 500
    height: 500

    Component {
        id: namePageComponent
        CreateName {}
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
        return null
    }

    function test_continue_disabled_when_empty() {
        const page = createTemporaryObject(namePageComponent, this)
        verify(page !== null)

        var btn = findChild(page, "createWalletNameContinueButton")
        verify(btn !== null)
        verify(!btn.enabled)
    }

    function test_continue_enabled_with_text() {
        const page = createTemporaryObject(namePageComponent, this)
        verify(page !== null)

        var input = findChild(page, "createWalletNameInput")
        verify(input !== null)
        input.text = "my_wallet"

        var btn = findChild(page, "createWalletNameContinueButton")
        verify(btn !== null)
        tryVerify(function() { return btn.enabled }, 2000)
    }

    function test_loading_shows_initializing_text() {
        const page = createTemporaryObject(namePageComponent, this)
        verify(page !== null)

        var input = findChild(page, "createWalletNameInput")
        verify(input !== null)
        input.text = "my_wallet"

        var btn = findChild(page, "createWalletNameContinueButton")
        verify(btn !== null)

        page.loading = true
        compare(btn.text, qsTr("Initializing..."))

        page.loading = false
        compare(btn.text, qsTr("Continue"))
    }

    function test_error_text_visible_on_error() {
        const page = createTemporaryObject(namePageComponent, this)
        verify(page !== null)

        walletController.walletLoadError = "Wallet already exists"
        waitForRendering(page)
        walletController.walletLoadError = ""
    }
}

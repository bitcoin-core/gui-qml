// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtTest 1.2
import "../../qml/pages/wallet"

TestCase {
    name: "CreateTypeSelector"
    when: windowShown
    width: 500
    height: 700

    Component {
        id: selectorComponent
        CreateTypeSelector {}
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

    function test_regular_selected_signal() {
        const page = createTemporaryObject(selectorComponent, this)
        verify(page !== null)

        var emitted = false
        page.regularSelected.connect(function() { emitted = true })

        var btn = findChild(page, "walletTypeRegular")
        verify(btn !== null)
        btn.clicked()
        verify(emitted)
    }

    function test_watchonly_selected_signal() {
        const page = createTemporaryObject(selectorComponent, this)
        verify(page !== null)

        var emitted = false
        page.watchOnlySelected.connect(function() { emitted = true })

        var btn = findChild(page, "walletTypeViewOnly")
        verify(btn !== null)
        btn.clicked()
        verify(emitted)
    }

    function test_import_selected_signal() {
        const page = createTemporaryObject(selectorComponent, this)
        verify(page !== null)

        var emitted = false
        page.importSelected.connect(function() { emitted = true })

        var btn = findChild(page, "walletTypeImport")
        verify(btn !== null)
        btn.clicked()
        verify(emitted)
    }

    function test_multikey_disabled() {
        const page = createTemporaryObject(selectorComponent, this)
        verify(page !== null)

        var btn = findChild(page, "walletTypeMultiKey")
        verify(btn !== null)
        verify(!btn.enabled)
    }

    function test_custom_disabled() {
        const page = createTemporaryObject(selectorComponent, this)
        verify(page !== null)

        var btn = findChild(page, "walletTypeCustom")
        verify(btn !== null)
        verify(!btn.enabled)
    }
}

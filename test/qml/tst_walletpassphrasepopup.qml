// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtTest 1.2
import "../../qml/components"

TestCase {
    name: "WalletPassphrasePopup"
    when: windowShown
    width: 500
    height: 320

    property string submittedPassphrase: ""
    property int submittedCount: 0

    Component {
        id: popupComponent

        WalletPassphrasePopup {
            popupObjectName: "testPassphrasePopup"
            titleText: "Enter wallet password"
            descriptionText: "Test password prompt"
            passphraseFieldObjectName: "testPassphraseField"
            cancelButtonObjectName: "testPassphraseCancelButton"
            confirmButtonObjectName: "testPassphraseConfirmButton"
        }
    }

    function findObjectByName(root, objectName) {
        if (!root) {
            return null
        }
        if (root.objectName === objectName) {
            return root
        }

        if (root.contentItem) {
            const contentResult = findObjectByName(root.contentItem, objectName)
            if (contentResult) {
                return contentResult
            }
        }

        const children = root.children || []
        for (let i = 0; i < children.length; ++i) {
            const childResult = findObjectByName(children[i], objectName)
            if (childResult) {
                return childResult
            }
        }

        return null
    }

    function createPopup() {
        submittedPassphrase = ""
        submittedCount = 0

        const popup = createTemporaryObject(popupComponent, this)
        verify(popup !== null)
        popup.submitted.connect(function(passphrase) {
            submittedPassphrase = passphrase
            submittedCount += 1
        })
        popup.open()
        tryCompare(popup, "opened", true)
        return popup
    }

    function test_submit_clears_field_after_emitting_passphrase() {
        const popup = createPopup()
        const field = findObjectByName(popup, "testPassphraseField")
        const button = findObjectByName(popup, "testPassphraseConfirmButton")
        verify(field !== null)
        verify(button !== null)

        const passphrase = "correct horse battery staple"
        field.text = passphrase
        verify(button.enabled)

        // An instant open leaves the first press to the next frame.
        waitForRendering(button, 1000)
        mouseClick(button, button.width / 2, button.height / 2)

        compare(submittedCount, 1)
        compare(submittedPassphrase, passphrase)
        compare(field.text, "")
    }

    function test_enter_or_return_submits_data() {
        return [
            { tag: "return", key: Qt.Key_Return },
            { tag: "enter", key: Qt.Key_Enter }
        ]
    }

    function test_enter_or_return_submits(data) {
        const popup = createPopup()
        const field = findObjectByName(popup, "testPassphraseField")
        verify(field !== null)
        tryCompare(field, "activeFocus", true)

        const passphrase = "correct horse battery staple"
        field.text = passphrase
        keyClick(data.key)

        compare(submittedCount, 1)
        compare(submittedPassphrase, passphrase)
        compare(field.text, "")
    }

    function test_return_does_not_submit_when_confirm_is_disabled_data() {
        return [
            { tag: "empty", passphrase: "", busy: false },
            { tag: "busy", passphrase: "secret", busy: true }
        ]
    }

    function test_return_does_not_submit_when_confirm_is_disabled(data) {
        const popup = createPopup()
        const field = findObjectByName(popup, "testPassphraseField")
        verify(field !== null)
        tryCompare(field, "activeFocus", true)

        field.text = data.passphrase
        popup.busy = data.busy
        keyClick(Qt.Key_Return)

        compare(submittedCount, 0)
        compare(field.text, data.passphrase)
    }

    function test_close_clears_field_without_submitting() {
        const popup = createPopup()
        const field = findObjectByName(popup, "testPassphraseField")
        verify(field !== null)

        field.text = "unused passphrase"
        popup.close()
        tryCompare(popup, "opened", false)

        compare(submittedCount, 0)
        compare(field.text, "")
    }

    function test_reopen_clears_stale_field_text() {
        const popup = createPopup()
        const field = findObjectByName(popup, "testPassphraseField")
        verify(field !== null)

        field.text = "stale passphrase"
        popup.close()
        tryCompare(popup, "opened", false)
        field.text = "stale passphrase"

        popup.open()
        tryCompare(popup, "opened", true)

        compare(field.text, "")
    }
}

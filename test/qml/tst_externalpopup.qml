// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtTest 1.2
import org.bitcoincore.qt 1.0
import "../../qml/controls"

TestCase {
    name: "ExternalPopup"
    when: windowShown
    width: 500
    height: 400

    Component {
        id: popupComponent

        ExternalPopup {
            link: "https://bitcoincore.org"
            width: 400
            height: 300
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

    function init() {
        UrlOpener.reset()
        Clipboard.setText("")
    }

    function createPopup() {
        const popup = createTemporaryObject(popupComponent, this)
        verify(popup !== null)
        return popup
    }

    // A successful open closes the popup and never enters the error state.
    function test_successful_open_closes_popup() {
        const popup = createPopup()
        UrlOpener.setOpenResult(true)
        popup.open()
        tryCompare(popup, "opened", true)

        const ok = findObjectByName(popup, "externalLinkConfirm")
        verify(ok !== null)
        mouseClick(ok, ok.width / 2, ok.height / 2)

        tryCompare(popup, "opened", false)
        verify(!popup.openFailed)
    }

    // A failed open keeps the popup open and reveals the copy-URL fallback.
    function test_failed_open_shows_copy_fallback() {
        const popup = createPopup()
        UrlOpener.setOpenResult(false)
        popup.open()
        tryCompare(popup, "opened", true)

        const ok = findObjectByName(popup, "externalLinkConfirm")
        verify(ok !== null)
        mouseClick(ok, ok.width / 2, ok.height / 2)

        verify(popup.openFailed)
        compare(popup.opened, true)

        const copy = findObjectByName(popup, "externalLinkCopy")
        verify(copy !== null)
        verify(copy.visible)
    }

    // The copy fallback writes the link to the clipboard.
    function test_copy_fallback_copies_link() {
        const popup = createPopup()
        UrlOpener.setOpenResult(false)
        popup.open()
        tryCompare(popup, "opened", true)
        popup.attemptOpen()
        verify(popup.openFailed)

        const copy = findObjectByName(popup, "externalLinkCopy")
        verify(copy !== null)
        verify(copy.visible)
        mouseClick(copy, copy.width / 2, copy.height / 2)

        verify(popup.linkCopied)
        compare(Clipboard.text(), "https://bitcoincore.org")
    }

    // A scheme outside http/https is refused before anything is handed to the
    // OS, and is reported to the user as an ordinary open failure so the
    // copy-URL fallback is still offered.
    function test_non_http_scheme_is_refused() {
        const popup = createPopup()
        UrlOpener.setOpenResult(true)
        popup.link = "file:///etc/passwd"
        popup.open()
        tryCompare(popup, "opened", true)

        popup.attemptOpen()

        verify(popup.openFailed)
        compare(popup.opened, true)
    }

    // Copying disables the button for the duration of the confirmation so the
    // label always describes what the button will do, then restores it.
    function test_copy_button_disables_then_restores() {
        const popup = createPopup()
        UrlOpener.setOpenResult(false)
        popup.copiedFeedbackMs = 120
        popup.open()
        tryCompare(popup, "opened", true)
        popup.attemptOpen()
        verify(popup.openFailed)

        const copy = findObjectByName(popup, "externalLinkCopy")
        verify(copy !== null)
        const label = copy.text
        verify(copy.enabled)

        popup.copyLink()

        verify(popup.linkCopied)
        compare(copy.enabled, false)
        verify(copy.text !== label)

        tryCompare(popup, "linkCopied", false)
        compare(copy.enabled, true)
        compare(copy.text, label)
    }

    // Reopening after a failure resets back to the confirmation state.
    function test_reopen_resets_failure_state() {
        const popup = createPopup()
        UrlOpener.setOpenResult(false)
        popup.open()
        tryCompare(popup, "opened", true)
        popup.attemptOpen()
        verify(popup.openFailed)

        popup.close()
        tryCompare(popup, "opened", false)

        popup.open()
        tryCompare(popup, "opened", true)
        verify(!popup.openFailed)
        verify(!popup.linkCopied)
    }
}

// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtTest 1.2
import "../../qml/controls"

TestCase {
    name: "SendOptionsPopup"
    when: windowShown
    width: 400
    height: 300

    Item {
        id: host
        width: parent.width
        height: parent.height
    }

    Component {
        id: popupComponent

        SendOptionsPopup {
            x: 20
            y: 20
        }
    }

    function createPopup() {
        const popup = createTemporaryObject(popupComponent, host)
        verify(popup !== null)
        popup.open()
        tryCompare(popup, "opened", true)
        wait(0)
        return popup
    }

    function findObject(root, objectName) {
        if (!root) return null
        if (root.objectName === objectName) return root

        const extraRoots = []
        if (root.contentItem) extraRoots.push(root.contentItem)
        if (root.background) extraRoots.push(root.background)

        for (let i = 0; i < extraRoots.length; ++i) {
            const extraMatch = findObject(extraRoots[i], objectName)
            if (extraMatch) return extraMatch
        }

        const children = root.children || []
        for (let i = 0; i < children.length; ++i) {
            const match = findObject(children[i], objectName)
            if (match) return match
        }
        return null
    }

    function test_aliases_default_to_false() {
        const popup = createPopup()
        compare(popup.coinControlEnabled, false)
        compare(popup.multipleRecipientsEnabled, false)
    }

    function test_alias_assignments_drive_toggle_state() {
        const popup = createPopup()

        popup.coinControlEnabled = true
        popup.multipleRecipientsEnabled = true
        compare(popup.coinControlEnabled, true)
        compare(popup.multipleRecipientsEnabled, true)
    }

    function test_clicking_toggles_updates_aliases() {
        const popup = createPopup()
        const coinToggle = findObject(popup, "sendOptionsCoinControlToggle")
        const multipleToggle = findObject(popup, "sendOptionsMultipleRecipientsToggle")

        verify(coinToggle !== null)
        verify(multipleToggle !== null)

        mouseClick(coinToggle, coinToggle.width / 2, coinToggle.height / 2)
        compare(popup.coinControlEnabled, true)

        mouseClick(multipleToggle, multipleToggle.width / 2, multipleToggle.height / 2)
        compare(popup.multipleRecipientsEnabled, true)
    }

    function test_action_rows_emit_signals() {
        const popup = createPopup()
        const importButton = findObject(popup, "sendImportPsbtFromFileButton")
        const clearButton = findObject(popup, "sendClearFormButton")

        verify(importButton !== null)
        verify(clearButton !== null)

        let importCount = 0
        let clearCount = 0
        popup.importPsbtFromFileRequested.connect(function() { importCount += 1 })
        popup.clearFormRequested.connect(function() { clearCount += 1 })

        mouseClick(importButton, importButton.width / 2, importButton.height / 2)
        compare(importCount, 1)

        mouseClick(clearButton, clearButton.width / 2, clearButton.height / 2)
        compare(clearCount, 1)
    }
}

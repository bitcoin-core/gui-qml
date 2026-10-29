// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtTest 1.2
import "../../qml/controls"
import "../../qml/components"

TestCase {
    id: testCase
    name: "ExternalSignerPathSection"
    when: windowShown
    visible: true
    width: 640
    height: 420

    property string motionMode: ""

    function init() {
        motionMode = Theme.motion.mode
        Theme.motion.mode = "full"
        walletController.reset()
        optionsModel.externalSignerPath = "/signer/signer.py"
        setStatus(true, 1, "trezor_t", "")
    }

    function cleanup() {
        Theme.motion.mode = motionMode
        optionsModel.externalSignerPath = ""
        walletController.reset()
    }

    function setStatus(responding, count, name, error) {
        walletController.externalSignerPathConfigured = true
        walletController.externalSignerResponding = responding
        walletController.externalSignerCount = count
        walletController.externalSignerName = name
        walletController.externalSignerError = error
        walletController.canCreateExternalSignerWallet = responding && count === 1 && error.length === 0
    }

    ExternalSignerPathSection {
        id: section
        width: 600
        namePrefix: "test"
    }

    function findItem(item, predicate) {
        for (let i = 0; i < item.children.length; ++i) {
            const child = item.children[i]
            if (predicate(child)) return child
            const found = findItem(child, predicate)
            if (found) return found
        }
        return null
    }

    // The status icon in a row.
    function statusIcon(rowName) {
        const icon = findItem(findChild(section, rowName), function(item) {
            return item.size !== undefined && item.source !== undefined && item.icon !== undefined
        })
        verify(icon !== null, rowName + " has no status icon")
        return icon
    }

    // Checks the icon's color and the color its image is drawn in. The
    // tests run on a platform where grabbing an item gives an empty image,
    // so they read colors, not pixels.
    function compareIcon(rowName, expected) {
        const icon = statusIcon(rowName)
        verify(Qt.colorEqual(icon.icon.color, expected),
               rowName + " icon color " + icon.icon.color + ", expected " + expected)
        const image = findItem(icon.contentItem, function(item) {
            return String(item).indexOf("QQuickIconImage") === 0
        })
        verify(image !== null, rowName + " has no icon image")
        verify(Qt.colorEqual(image.color, expected),
               rowName + " icon image color " + image.color + ", expected " + expected)
    }

    function test_iconsShowTheirStateAfterAPathChange() {
        wait(400)
        compareIcon("testCommandRow", Theme.color.successText)
        compareIcon("testDeviceRow", Theme.color.successText)

        // Pick another signer, as Browse does, then check it. The new
        // result arrives while the check runs.
        findChild(section, "testPathInput").text = "/signer/multi_signers.py"
        wait(400)
        section.check()
        setStatus(true, 2, "", "More than one external signer was found. Connect only one device.")
        wait(400)
        compareIcon("testCommandRow", Theme.color.successText)
        compareIcon("testDeviceRow", Theme.color.warningText)
    }

    Component {
        id: iconComponent
        Icon {
            source: "image://images/check"
            color: Theme.color.successText
        }
    }

    // Only a change of image together with color skips the fade; a change of
    // color alone, such as a hover tint, still fades and lands on the color.
    function test_colorOnlyChangeStillFades() {
        const icon = createTemporaryObject(iconComponent, testCase)
        verify(icon !== null)
        wait(50)
        icon.color = Theme.color.dangerText
        verify(!Qt.colorEqual(icon.icon.color, Theme.color.dangerText), "changed at once instead of fading")
        tryVerify(function() { return Qt.colorEqual(icon.icon.color, Theme.color.dangerText) }, 1000)

        // A new image takes its color at once, whichever of the two changes
        // first (bindings don't promise an order).
        icon.source = "image://images/alert-filled"
        icon.color = Theme.color.warningText
        verify(Qt.colorEqual(icon.icon.color, Theme.color.warningText), "faded with a new image")
        wait(50)
        icon.color = Theme.color.successText
        icon.source = "image://images/check"
        verify(Qt.colorEqual(icon.icon.color, Theme.color.successText), "faded with a new image")
    }
}

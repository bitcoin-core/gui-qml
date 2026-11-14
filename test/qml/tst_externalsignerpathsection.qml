// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtTest 1.2
import org.bitcoincore.qt 1.0
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
        externalSignerModel.reset()
        externalSignerModel.draftPath = "/signer/signer.py"
        setStatus(ExternalSignerModel.Ready, "trezor_t", "")
        section = createTemporaryObject(sectionComponent, testCase)
        verify(section !== null)
    }

    function cleanup() {
        Theme.motion.mode = motionMode
        externalSignerModel.reset()
    }

    function setStatus(status, name, error) {
        externalSignerModel.deviceName = name
        externalSignerModel.error = error
        externalSignerModel.status = status
    }

    // A new section for each test, so one test's field doesn't carry into
    // the next.
    property var section: null
    Component {
        id: sectionComponent
        ExternalSignerPathSection {
            width: 600
            namePrefix: "test"
        }
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
        externalSignerModel.status = ExternalSignerModel.NotChecked
        wait(400)
        externalSignerModel.holdChecks = true
        section.check()
        setStatus(ExternalSignerModel.MultipleDevices, "", "More than one external signer was found. Connect only one device.")
        externalSignerModel.finishCheckForTest()
        wait(400)
        compareIcon("testCommandRow", Theme.color.successText)
        compareIcon("testDeviceRow", Theme.color.warningIcon)
    }

    Component {
        id: iconComponent
        Icon {
            source: "image://images/check"
            color: Theme.color.successText
        }
    }

    Component {
        id: stateIconComponent
        Icon {
            source: "image://images/check"
            color: Theme.color.successText
            animatesColor: false
        }
    }

    // A change of color, such as a hover tint, fades and lands on the color.
    // An icon that shows a state takes a new color at once, with or without
    // a new image.
    function test_colorFadesUnlessTheIconShowsAState() {
        const icon = createTemporaryObject(iconComponent, testCase)
        verify(icon !== null)
        wait(50)
        icon.color = Theme.color.dangerText
        verify(!Qt.colorEqual(icon.icon.color, Theme.color.dangerText), "changed at once instead of fading")
        tryVerify(function() { return Qt.colorEqual(icon.icon.color, Theme.color.dangerText) }, 1000)

        const stateIcon = createTemporaryObject(stateIconComponent, testCase)
        verify(stateIcon !== null)
        wait(50)
        stateIcon.color = Theme.color.dangerText
        verify(Qt.colorEqual(stateIcon.icon.color, Theme.color.dangerText), "faded")
        stateIcon.source = "image://images/alert-filled"
        stateIcon.color = Theme.color.warningText
        verify(Qt.colorEqual(stateIcon.icon.color, Theme.color.warningText), "faded with a new image")
        wait(50)
        stateIcon.color = Theme.color.successText
        stateIcon.source = "image://images/check"
        verify(Qt.colorEqual(stateIcon.icon.color, Theme.color.successText), "faded with a new image")
    }

    // A list row's icon whose image changes while its page is torn down, as
    // Activity's rows do. The list clears the row's QML context at once but
    // deletes the row later, so nothing the icon runs later may rely on it.
    Component {
        id: iconListPageComponent
        Item {
            id: iconListPage
            width: 100
            height: 100
            property string iconName: "check"
            ListView {
                anchors.fill: parent
                model: 1
                delegate: Item {
                    width: 40
                    height: 40
                    Icon {
                        source: "image://images/" + iconListPage.iconName
                        color: Theme.color.successText
                    }
                }
            }
            Component.onDestruction: iconListPage.iconName = "alert-filled"
        }
    }

    function test_iconImageChangedWhileItsPageIsTornDown() {
        // failOnWarning() exists from Qt 6.3; older Qt only tears the page down.
        if (typeof failOnWarning === "function") failOnWarning(/Icon\.qml/)
        const page = iconListPageComponent.createObject(testCase)
        verify(page !== null)
        wait(50)
        page.destroy()
        // Deletes the page, then runs anything left queued.
        wait(0)
        wait(50)
    }

    // The check and the alert leave a wide margin in their images while the
    // cross fills its own, so each gets the box that draws its shape at the
    // cross's size: about 0.56 and 0.6 of the box are shape, against all of it.
    function compareIconBox(rowName, source, box) {
        tryVerify(function() { return String(statusIcon(rowName).source) === source }, 3000,
                  rowName + " never showed " + source)
        const icon = statusIcon(rowName)
        compare(icon.size, box, rowName + " box for " + source)
        compare(icon.width, box, rowName + " width for " + source)
        compare(icon.height, box, rowName + " height for " + source)
    }

    function test_statusIconsDrawAtOneSize() {
        Theme.motion.mode = "off"
        compareIconBox("testCommandRow", "image://images/check", 32)

        findChild(section, "testPathInput").text = "/signer/multi_signers.py"
        section.check()
        setStatus(ExternalSignerModel.MultipleDevices, "", "More than one external signer was found. Connect only one device.")
        compareIconBox("testDeviceRow", "image://images/alert-filled", 30)

        findChild(section, "testPathInput").text = "/signer/not_a_signer.py"
        section.check()
        setStatus(ExternalSignerModel.NotResponding, "", "The signer command did not return valid output. Check that the path is correct.")
        compareIconBox("testCommandRow", "image://images/cross-circle-filled", 18)
    }

    Component {
        id: secondSectionComponent
        ExternalSignerPathSection {
            width: 600
            namePrefix: "second"
        }
    }

    // Settings and wallet creation can both be open, each with a section.
    // They show one path: an edit in one, a path chosen with Browse, or one
    // the model puts back reaches the other field, including a field whose
    // text was typed.
    function test_sectionsShareOnePath() {
        const second = createTemporaryObject(secondSectionComponent, testCase)
        verify(second !== null)
        const field = findChild(section, "testPathInput")
        const secondField = findChild(second, "secondPathInput")
        compare(secondField.text, "/signer/signer.py")

        field.text = "/opt/hwi/hwi"
        compare(externalSignerModel.draftPath, "/opt/hwi/hwi")
        compare(secondField.text, "/opt/hwi/hwi")

        secondField.text = "/opt/hwi/hwi2"
        compare(field.text, "/opt/hwi/hwi2")

        findChild(second, "secondPathDialog").selectAndAccept("/opt/hwi/hwi3")
        compare(field.text, "/opt/hwi/hwi3")
        compare(secondField.text, "/opt/hwi/hwi3")

        externalSignerModel.savedPath = "/signer/signer.py"
        externalSignerModel.revertDraft()
        compare(field.text, "/signer/signer.py")
        compare(secondField.text, "/signer/signer.py")
    }

    // A check saves and runs the path it started with, so neither the field
    // nor Browse can change the path until the result is in.
    function test_pathIsLockedWhileChecking() {
        const field = findChild(section, "testPathInput")
        const browse = findChild(section, "testBrowseButton")
        compare(field.enabled, true)
        compare(browse.enabled, true)
        externalSignerModel.holdChecks = true
        section.check()
        compare(section.checking, true)
        compare(field.enabled, false)
        compare(browse.enabled, false)
        externalSignerModel.finishCheckForTest()
        compare(field.enabled, true)
        compare(browse.enabled, true)
    }
}

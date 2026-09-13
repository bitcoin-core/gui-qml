// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.2

import "../../qml/controls"

TestCase {
    name: "CopyButton"
    when: windowShown
    width: 300
    height: 200

    Window {
        id: testWindow
        width: 300
        height: 200
        visible: true
    }

    SignalSpy {
        id: copyRequestedSpy
        signalName: "copyRequested"
    }

    Component {
        id: buttonComponent

        CopyButton {
            objectName: "copyButton"
        }
    }

    function createButton(properties) {
        const button = createTemporaryObject(
            buttonComponent, testWindow.contentItem, properties || {})
        verify(button !== null)
        return button
    }

    function test_defaultsToIconAndText() {
        const button = createButton()
        const content = findChild(button, "copyButtonContent")
        const iconContainer = findChild(button, "copyButtonIconContainer")
        const copyIcon = findChild(button, "copyButtonCopyIcon")
        const textContainer = findChild(button, "copyButtonTextContainer")

        compare(button.displayMode, CopyButton.IconAndText)
        compare(button.text, "Copy")
        compare(button.copied, false)
        compare(button.iconSize, 16)
        compare(button.resetInterval, 1000)
        compare(content.spacing, 0)
        compare(copyIcon.size, 16)
        compare(iconContainer.width, 18)
        compare(iconContainer.visible, true)
        compare(textContainer.visible, true)
    }

    function test_displayModes_data() {
        return [
            {
                tag: "icon only",
                displayMode: CopyButton.IconOnly,
                iconVisible: true,
                textVisible: false
            },
            {
                tag: "text only",
                displayMode: CopyButton.TextOnly,
                iconVisible: false,
                textVisible: true
            },
            {
                tag: "icon and text",
                displayMode: CopyButton.IconAndText,
                iconVisible: true,
                textVisible: true
            }
        ]
    }

    function test_displayModes(data) {
        const button = createButton({ "displayMode": data.displayMode })
        const iconContainer = findChild(button, "copyButtonIconContainer")
        const textContainer = findChild(button, "copyButtonTextContainer")

        compare(iconContainer.visible, data.iconVisible)
        compare(textContainer.visible, data.textVisible)
    }

    function test_clickShowsCopiedThenResets() {
        const button = createButton({ "resetInterval": 300 })
        const copyIcon = findChild(button, "copyButtonCopyIcon")
        const copiedIcon = findChild(button, "copyButtonCopiedIcon")
        const copyText = findChild(button, "copyButtonCopyText")
        const copiedText = findChild(button, "copyButtonCopiedText")

        copyRequestedSpy.target = button
        copyRequestedSpy.clear()
        button.clicked()
        compare(copyRequestedSpy.count, 1)
        compare(button.copied, true)
        compare(button.text, "Copied")
        tryCompare(copyIcon, "opacity", 0)
        tryCompare(copiedIcon, "opacity", 1)
        tryCompare(copyText, "opacity", 0)
        tryCompare(copiedText, "opacity", 1)

        tryCompare(button, "copied", false, 500)
        compare(button.text, "Copy")
        tryCompare(copyIcon, "opacity", 1)
        tryCompare(copiedIcon, "opacity", 0)
        tryCompare(copyText, "opacity", 1)
        tryCompare(copiedText, "opacity", 0)
    }
}

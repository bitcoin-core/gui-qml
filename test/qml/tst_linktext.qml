// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtTest 1.2
import "../../qml/controls"

TestCase {
    id: testCase
    name: "LinkText"
    when: windowShown
    visible: true
    width: 400
    height: 200

    Component {
        id: linkTextComponent
        Column {
            property alias linkText: linkText
            property alias before: before
            width: 360
            Button { id: before; text: "Before" }
            LinkText {
                id: linkText
                width: 360
                sentence: "Read the %1 first."
                linkText: "setup <guide>"
                link: "https://example.com/guide?a=1&b=2"
            }
        }
    }

    SignalSpy {
        id: activatedSpy
        signalName: "activated"
    }

    function init() {
        activatedSpy.clear()
    }

    function test_linkSitsInsideTheSentence() {
        const column = createTemporaryObject(linkTextComponent, testCase)
        const linkText = column.linkText
        // The link words and address are escaped, so they can't change the markup.
        // The external-link icon follows the link, drawn in the link color.
        const iconSize = linkText.iconSize
        compare(linkText.text,
                "Read the <a href=\"https://example.com/guide?a=1&amp;b=2\">setup &lt;guide&gt;</a>"
                + "<img src=\"image://images/export?color=" + Theme.color.neutral9.toString().substring(1, 7)
                + "\" width=\"" + iconSize + "\" height=\"" + iconSize + "\" align=\"middle\"> first.")
        compare(linkText.linkColor, Theme.color.neutral9)
        compare(linkText.Accessible.role, Accessible.Link)
        compare(linkText.Accessible.name, "setup <guide>")
        compare(linkText.Accessible.description, "Read the setup <guide> first.")
    }

    function test_mouseAndKeyboardOpenTheLink() {
        const column = createTemporaryObject(linkTextComponent, testCase)
        const linkText = column.linkText
        activatedSpy.target = linkText

        linkText.linkActivated("https://example.com/guide?a=1&b=2")
        compare(activatedSpy.count, 1)
        compare(activatedSpy.signalArguments[0][0].toString(), "https://example.com/guide?a=1&b=2")

        // One Tab stop after the control before it; Enter and Space open the link.
        column.before.forceActiveFocus()
        keyClick(Qt.Key_Tab)
        verify(linkText.activeFocus)
        keyClick(Qt.Key_Return)
        keyClick(Qt.Key_Space)
        compare(activatedSpy.count, 3)
        compare(activatedSpy.signalArguments[2][0].toString(), "https://example.com/guide?a=1&b=2")
    }

    function test_disabledLinkDoesNothing() {
        const column = createTemporaryObject(linkTextComponent, testCase)
        const linkText = column.linkText
        activatedSpy.target = linkText
        linkText.enabled = false
        compare(linkText.activeFocusOnTab, false)
        linkText.linkActivated("https://example.com/guide?a=1&b=2")
        compare(activatedSpy.count, 0)
    }

    Component {
        id: endingLinkComponent
        LinkText {
            width: 360
            sentence: "Follow the %1"
            linkText: "setup guide."
            link: "https://example.com/guide"
        }
    }

    // A link that ends its sentence carries the period before the icon, but
    // is named without it.
    function test_linkEndingTheSentenceKeepsItsPeriodOutOfTheName() {
        const linkText = createTemporaryObject(endingLinkComponent, testCase)
        verify(linkText.text.indexOf(">setup guide.</a><img") > 0)
        compare(linkText.Accessible.name, "setup guide")
    }

    // Clicks, the cursor and the focus ring cover the link's words and its
    // icon, not the whole sentence.
    function test_linkAreaCoversTheLinkAndItsIcon() {
        const column = createTemporaryObject(linkTextComponent, testCase)
        const linkText = column.linkText
        activatedSpy.target = linkText
        tryVerify(function() { return linkText.linkAreas.length > 0 })
        compare(linkText.linkAreas.length, 1)
        const area = linkText.linkAreas[0]
        // Starts at the link, after "Read the ", and stops well short of the line.
        verify(area.x > 0)
        verify(area.width < linkText.contentWidth)
        // Reaches past the link's words, over the icon.
        verify(linkText.linkAt(area.x + area.width - 2, area.y + area.height / 2) === "")
        mouseClick(linkText, area.x + area.width - 3, area.y + area.height / 2)
        compare(activatedSpy.count, 1)
        mouseClick(linkText, 1, area.y + area.height / 2)
        compare(activatedSpy.count, 1)
    }

    Component {
        id: wrappedLinkComponent
        LinkText {
            width: 180
            // Short words, so the link starts on the first line, right after
            // "Read", and wraps whatever the font.
            sentence: "Read %1 first."
            linkText: "how to set up a device to use it as a signer for a new wallet"
            link: "https://example.com/guide"
        }
    }

    // A link that wraps gets an area on each of its lines, so the sentence
    // before it on its first line stays plain text.
    function test_wrappedLinkLeavesTheSentenceAroundItPlain() {
        const linkText = createTemporaryObject(wrappedLinkComponent, testCase)
        activatedSpy.target = linkText
        tryVerify(function() { return linkText.linkAreas.length > 1 }, 3000, "the link should wrap")
        const first = linkText.linkAreas[0]
        const last = linkText.linkAreas[linkText.linkAreas.length - 1]
        // The link starts after sentence text on its first line, and its
        // next line starts at the left edge.
        verify(first.x > 4, "first line area " + first)
        verify(last.y > first.y)

        // The link's words open it on each of its lines.
        mouseClick(linkText, first.x + 2, first.y + first.height / 2)
        compare(activatedSpy.count, 1)
        mouseClick(linkText, last.x + 2, last.y + last.height / 2)
        compare(activatedSpy.count, 2)
        // The sentence before the link, on the same line, does not.
        mouseClick(linkText, 1, first.y + first.height / 2)
        compare(activatedSpy.count, 2)
    }
}

// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtTest 1.2
import "../../qml/controls"

TestCase {
    name: "HoverTooltip"
    when: windowShown
    width: 400
    height: 300

    Component {
        id: iconButtonComponent

        IconButton {
            objectName: "tooltipTestButton"
            x: 100
            y: 100
            iconSource: "qrc:/icons/file"
            tooltipText: "Export activity to CSV"
        }
    }

    Component {
        id: plainIconButtonComponent

        IconButton {
            objectName: "plainTestButton"
            x: 100
            y: 100
            iconSource: "qrc:/icons/file"
        }
    }

    Component {
        id: hoverableComponent

        Item {
            id: hoverable
            objectName: "hoverable"
            x: 50
            y: 100
            width: 20
            height: 20

            property bool hovered: false
            property alias tooltip: tooltip

            HoverTooltip {
                id: tooltip
                objectName: "hoverableTooltip"
                text: "Clear console input or output"
                below: false
            }
        }
    }

    function findObject(item, objectName) {
        if (!item) return null
        if (item.objectName === objectName) return item
        const children = item.children || []
        for (let i = 0; i < children.length; ++i) {
            const result = findObject(children[i], objectName)
            if (result) return result
        }
        return null
    }

    function test_icon_button_wires_its_tooltip_to_itself() {
        const button = createTemporaryObject(iconButtonComponent, this)
        verify(button !== null)

        const tooltip = findObject(button, "tooltipTestButtonTooltip")
        verify(tooltip !== null)
        compare(tooltip.target, button)
        compare(tooltip.text, "Export activity to CSV")
        verify(tooltip.below)
    }

    function test_tooltip_tracks_the_target_hover_state() {
        const item = createTemporaryObject(hoverableComponent, this)
        const tooltip = item.tooltip

        compare(tooltip.target, item)
        verify(!tooltip.shown)
        verify(!tooltip.active)
        compare(tooltip.item, null)

        item.hovered = true
        verify(tooltip.shown)
        verify(tooltip.active)
        verify(tooltip.item !== null)
        compare(tooltip.item.text, "Clear console input or output")

        item.hovered = false
        verify(!tooltip.shown)
        verify(!tooltip.active)
        compare(tooltip.item, null)
    }

    function test_no_tooltip_without_text() {
        const button = createTemporaryObject(plainIconButtonComponent, this)
        const tooltip = findObject(button, "plainTestButtonTooltip")
        verify(tooltip !== null)
        compare(tooltip.text, "")

        tooltip.shown = true
        verify(!tooltip.active)
        compare(tooltip.item, null)
    }

    function test_tooltip_is_centered_below_the_target_by_default() {
        const button = createTemporaryObject(iconButtonComponent, this)
        const tooltip = findObject(button, "tooltipTestButtonTooltip")
        verify(tooltip !== null)

        tooltip.shown = true
        verify(tooltip.item !== null)

        compare(tooltip.y, button.height)
        fuzzyCompare(tooltip.x + tooltip.width / 2, button.width / 2, 1)
        verify(!tooltip.item.arrowAtBottom)
        verify(tooltip.z > 0)
    }

    function test_bubble_hangs_left_of_the_arrow_so_it_stays_on_screen() {
        const button = createTemporaryObject(iconButtonComponent, this)
        const tooltip = findChild(button, "tooltipTestButtonTooltip")

        tooltip.shown = true
        verify(tooltip.item !== null)
        verify(!tooltip.item.centerBubbleOnArrow)

        const bubbleRightOfArrow = tooltip.item.arrowWidth / 2 + tooltip.item.arrowHorizontalInset
        verify(tooltip.width > button.width * 2)
        verify(bubbleRightOfArrow < button.width)
    }

    function test_tooltip_can_be_placed_above_the_target() {
        const item = createTemporaryObject(hoverableComponent, this)
        const tooltip = item.tooltip

        item.hovered = true
        verify(tooltip.item !== null)

        compare(tooltip.y + tooltip.height, 0)
        fuzzyCompare(tooltip.x + tooltip.width / 2, item.width / 2, 1)
        verify(tooltip.item.arrowAtBottom)
    }
}

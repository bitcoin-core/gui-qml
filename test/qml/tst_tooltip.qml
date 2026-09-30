// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.2
import "../../qml/components"
import "../../qml/controls"

TestCase {
    name: "Tooltip"
    when: windowShown
    visible: true
    width: 200
    height: 100

    Component {
        id: tooltipComponent
        Tooltip {
            text: "Details"
            shown: false
        }
    }

    Component {
        id: anchoredTooltipComponent
        Item {
            width: 20
            height: 20
            Tooltip {
                objectName: "anchoredTooltip"
                anchors.horizontalCenter: parent.horizontalCenter
                text: "Settings"
            }
        }
    }

    function test_theme_and_open_close() {
        const tooltip = createTemporaryObject(tooltipComponent, this)
        verify(tooltip !== null)
        compare(tooltip.backgroundColor, Theme.color.neutral1)
        compare(tooltip.borderColor, Theme.color.neutral2)
        compare(tooltip.visible, false)

        tooltip.shown = true
        compare(tooltip.visible, false)
        tryCompare(tooltip, "opacity", 1)
        compare(tooltip.visible, true)
        compare(tooltip.scale, 1)

        tooltip.shown = false
        compare(tooltip.visible, true)
        tryCompare(tooltip, "visible", false)
        compare(tooltip.opacity, 0)
    }

    function test_cancel_pending_show() {
        const tooltip = createTemporaryObject(tooltipComponent, this)
        verify(tooltip !== null)

        tooltip.shown = true
        tooltip.shown = false
        wait(100)
        compare(tooltip.visible, false)
    }

    function test_motion_can_be_disabled() {
        const tooltip = createTemporaryObject(tooltipComponent, this)
        verify(tooltip !== null)

        tooltip.motionEnabled = false
        tooltip.shown = true
        compare(tooltip.visible, true)
        compare(tooltip.opacity, 1)
        compare(tooltip.scale, 1)

        tooltip.shown = false
        compare(tooltip.visible, false)
        compare(tooltip.opacity, 0)
    }

    function test_stays_inside_window() {
        const anchor = createTemporaryObject(anchoredTooltipComponent, this,
            { x: Window.window.width - 20 })
        verify(anchor !== null)
        const tooltip = findChild(anchor, "anchoredTooltip")
        verify(tooltip !== null)
        wait(0)
        let left = tooltip.mapToItem(null, 0, 0).x
        verify(left + tooltip.width <= Window.window.width - tooltip.windowMargin + 0.5)

        anchor.x = 0
        wait(0)
        left = tooltip.mapToItem(null, 0, 0).x
        verify(left >= tooltip.windowMargin - 0.5)

        tooltip.text = "This is a long tooltip in a narrow window"
        wait(0)
        verify(tooltip.width <= Window.window.width - 2 * tooltip.windowMargin)
        left = tooltip.mapToItem(null, 0, 0).x
        verify(left >= tooltip.windowMargin - 0.5)
        verify(left + tooltip.width <= Window.window.width - tooltip.windowMargin + 0.5)
    }
}

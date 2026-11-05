// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.2
import "../../qml/controls"

TestCase {
    name: "NavigationSplitView"
    when: windowShown
    width: 960
    height: 720

    property string motionMode: ""

    // TestCase itself is never visible, so the columns' visibility is
    // checked in a window of their own.
    Window {
        width: 960
        height: 720
        visible: true

        Item { id: host; anchors.fill: parent }

        Item {
            id: outsideFocus
            width: 10
            height: 10
            activeFocusOnTab: true
        }
    }

    Component {
        id: hiddenParentComponent
        Item { anchors.fill: parent; visible: false }
    }

    Component {
        id: splitComponent
        NavigationSplitView {
            width: 900
            height: 600
            transitionDuration: 0
            primaryMinimumWidth: 280
            primaryPreferredWidth: 320
            primaryMaximumWidth: 498
            primaryWidthRatio: 0.4
            detailMinimumWidth: 240
            primaryComponent: Rectangle { objectName: "testPrimary" }
            detailComponent: Rectangle { objectName: "testDetail" }
        }
    }

    Component {
        id: focusSplitComponent
        NavigationSplitView {
            width: 390
            height: 600
            transitionDuration: 0
            primaryComponent: FocusScope {
                Item {
                    objectName: "primaryField"
                    width: 10
                    height: 10
                    focus: true
                    activeFocusOnTab: true
                }
            }
            detailComponent: FocusScope {
                Item {
                    objectName: "detailField"
                    width: 10
                    height: 10
                    focus: true
                    activeFocusOnTab: true
                }
            }
        }
    }

    function init() {
        motionMode = Theme.motion.mode
    }

    function cleanup() {
        Theme.motion.mode = motionMode
    }

    function createSplit(properties) {
        const split = createTemporaryObject(splitComponent, host, properties || {})
        verify(split !== null)
        wait(0)
        return split
    }

    function test_regularUsesFixedColumnsAndFullHeightSeparator() {
        const split = createSplit()
        const primary = findChild(split, "navigationSplitPrimary")
        const detail = findChild(split, "navigationSplitDetail")
        compare(split.isCompact, false)
        compare(primary.width, 360)
        compare(primary.x, 0)
        compare(detail.x, 361)
        compare(detail.width, 539)

        const separator = findChild(split, "navigationSplitSeparator")
        verify(separator !== null)
        compare(separator.visible, true)
        compare(separator.width, 1)
        compare(separator.height, 600)
    }

    function test_equalWidthLimitsFixThePrimaryColumnWithoutASeparator() {
        const split = createSplit({
            "primaryMinimumWidth": 286,
            "primaryPreferredWidth": 286,
            "primaryMaximumWidth": 286,
            "separatorWidth": 0
        })
        const primary = findChild(split, "navigationSplitPrimary")
        const detail = findChild(split, "navigationSplitDetail")
        const separator = findChild(split, "navigationSplitSeparator")

        const widths = [SizeClass.compactWidthMax, 900, 1500]
        for (let index = 0; index < widths.length; ++index) {
            split.width = widths[index]
            wait(0)
            compare(split.isCompact, false)
            compare(primary.width, 286)
            compare(detail.x, 286)
            compare(detail.width, widths[index] - 286)
            compare(separator.visible, false)
        }
    }

    function test_compactNavigatesAndRetainsDetailAcrossResize() {
        const split = createSplit()
        const primary = findChild(split, "navigationSplitPrimary")
        const detail = findChild(split, "navigationSplitDetail")
        split.width = 390
        wait(0)
        compare(split.isCompact, true)
        compare(primary.x, 0)
        compare(detail.x, 390)

        split.showDetail()
        wait(0)
        compare(primary.x, -390)
        compare(detail.x, 0)

        split.width = 900
        wait(0)
        compare(split.isCompact, false)
        compare(primary.x, 0)
        verify(detail.x > 0)

        split.width = 390
        wait(0)
        compare(split.isCompact, true)
        compare(detail.x, 0)

        split.showPrimary()
        wait(0)
        compare(primary.x, 0)
        compare(detail.x, 390)
    }

    function test_compactUnderAHiddenParentShowsBothColumnsOnceRegular() {
        // Settings is created inside a hidden tab at zero width, so it
        // starts compact under an invisible ancestor.
        const hiddenParent = createTemporaryObject(hiddenParentComponent, host)
        const split = createTemporaryObject(splitComponent, hiddenParent, { width: 0 })
        wait(0)
        compare(split.isCompact, true)

        hiddenParent.visible = true
        split.width = 900
        wait(0)
        compare(split.isCompact, false)
        const primary = findChild(split, "navigationSplitPrimary")
        const detail = findChild(split, "navigationSplitDetail")
        verify(primary.visible)
        verify(primary.enabled)
        verify(detail.visible)
        verify(detail.enabled)
    }

    function test_compactHidesTheOffscreenColumnOnceItsTransitionEnds_data() {
        return [
            { tag: "full", mode: "full" },
            { tag: "reduced", mode: "reduced" },
            { tag: "off", mode: "off" }
        ]
    }

    function test_compactHidesTheOffscreenColumnOnceItsTransitionEnds(data) {
        Theme.motion.mode = data.mode
        const split = createSplit({ "width": 390, "transitionDuration": 120 })
        const primary = findChild(split, "navigationSplitPrimary")
        const detail = findChild(split, "navigationSplitDetail")
        const primaryItem = split.primaryItem
        const detailItem = split.detailItem
        verify(primaryItem !== null)
        verify(detailItem !== null)
        compare(primary.visible, true)
        compare(detail.visible, false)
        compare(detail.enabled, false)

        split.showDetail()
        compare(detail.visible, true)
        compare(detail.enabled, true)
        compare(primary.enabled, false)
        // The outgoing column stays drawn while it slides or fades away.
        compare(primary.visible, data.mode !== "off")
        tryCompare(primary, "visible", false)
        compare(detail.x, 0)
        compare(detail.opacity, 1)
        compare(primary.opacity, data.mode === "full" ? 1 : 0)
        compare(split.primaryItem, primaryItem)

        split.showPrimary()
        compare(primary.visible, true)
        tryCompare(detail, "visible", false)
        compare(primary.x, 0)
        compare(primary.opacity, 1)
        compare(split.detailItem, detailItem)

        split.width = 900
        compare(primary.visible, true)
        compare(detail.visible, true)
        compare(detail.enabled, true)
        compare(detail.opacity, 1)
    }

    function test_compactSwitchCarriesFocusOutOfTheHiddenColumn() {
        const split = createTemporaryObject(focusSplitComponent, host)
        verify(split !== null)
        const primaryField = findChild(split, "primaryField")
        const detailField = findChild(split, "detailField")
        verify(primaryField !== null)
        verify(detailField !== null)

        primaryField.forceActiveFocus()
        verify(primaryField.activeFocus)
        split.showDetail()
        verify(detailField.activeFocus)

        split.showPrimary()
        verify(primaryField.activeFocus)

        // Focus outside the split view stays where it is.
        outsideFocus.forceActiveFocus()
        split.showDetail()
        verify(outsideFocus.activeFocus)
        split.showPrimary()
        verify(outsideFocus.activeFocus)

        // Regular widths show both columns, so focus does not move.
        split.width = 900
        primaryField.forceActiveFocus()
        split.showDetail()
        verify(primaryField.activeFocus)
    }

    function test_compactBelowTheSharedThreshold() {
        const split = createSplit()
        split.width = SizeClass.compactWidthMax - 1
        wait(0)
        compare(split.isCompact, true)
        compare(SizeClass.isCompact(split.width), true)
        compare(SizeClass.widthClassFor(split.width), SizeClass.compact)

        split.width = SizeClass.compactWidthMax
        wait(0)
        compare(split.isCompact, false)
        compare(SizeClass.isCompact(split.width), false)
        compare(SizeClass.widthClassFor(split.width), SizeClass.regular)
    }
}

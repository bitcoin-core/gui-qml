// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtTest 1.2
import "../../qml/controls"

TestCase {
    name: "NavigationSplitView"
    when: windowShown
    width: 960
    height: 720

    Item { id: host; anchors.fill: parent }

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

    function createSplit() {
        const split = createTemporaryObject(splitComponent, host)
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
        compare(separator.width, 1)
        compare(separator.height, 600)
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
}

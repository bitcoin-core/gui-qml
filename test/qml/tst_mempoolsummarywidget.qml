// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
import QtQuick 2.15
import QtTest 1.2
import "../../qml/components/widgets"
import "../../qml/controls"

TestCase {
    id: testCase
    name: "MempoolSummaryWidget"
    when: windowShown
    visible: true
    width: 600
    height: 450
    QtObject {
        id: activity
        property bool ready: true
        property bool active: true
        property bool summaryActive: false
        property real queuedVbytes: 1234567
        property real minimumFee: 0.1
    }
    QtObject {
        id: node
        property bool mempoolInformationAvailable: true
        property int mempoolTransactionCount: 518
        property real mempoolUsageMB: 1.86
        property real mempoolMaxUsageMB: 300
    }
    Component { id: fixture; MempoolSummaryWidget { activityModel: activity; nodeModelRef: node } }
    function init() { activity.ready = true; activity.queuedVbytes = 1234567; node.mempoolInformationAvailable = true; node.mempoolTransactionCount = 1033; Theme.dark = true }
    function test_sizes_data() {
        return [
            {tag: "compact", columns: 1, rows: 1, width: 112, height: 112, dark: true},
            {tag: "small", columns: 2, rows: 1, width: 260, height: 112, dark: true},
            {tag: "medium", columns: 3, rows: 2, width: 392, height: 244, dark: true},
            {tag: "medium-light", columns: 3, rows: 2, width: 392, height: 244, dark: false}
        ]
    }
    function test_sizes(data) {
        Theme.dark = data.dark
        const widget = createTemporaryObject(fixture, testCase, {columnSpan: data.columns, rowSpan: data.rows, width: data.width, height: data.height})
        waitForRendering(widget)
        waitForPolish(widget)
        compare(widget.queuedBlocks, 2)
        for (const name of ["mempoolSummaryCount", "mempoolSummaryFee", "mempoolSummaryMemory", "mempoolQueuedBlocks", "mempoolSummaryCompactFee", "mempoolSummaryCountLabel", "mempoolSummaryInlineCountLabel", "mempoolQueuedBlocksLabel"]) {
            const item = findChild(widget, name)
            if (!item.visible) continue
            const point = item.mapToItem(widget, 0, 0)
            verify(point.x >= 0 && point.y >= 0, name)
            verify(point.x + item.width <= widget.width + 1, name)
            verify(point.y + item.height <= widget.height + 1, name)
            verify(!item.truncated, name)
        }
    }
    function test_hierarchyAndResize() {
        const widget = createTemporaryObject(fixture, testCase, {columnSpan: 1, rowSpan: 1, width: 112, height: 112})
        waitForPolish(widget)
        const compactFee = findChild(widget, "mempoolSummaryCompactFee")
        verify(compactFee.visible)
        compare(compactFee.text, "0.1 sat/vB")
        verify(!findChild(widget, "mempoolQueuedBlocks").visible)
        widget.columnSpan = 2
        widget.width = 260
        waitForPolish(widget)
        const count = findChild(widget, "mempoolSummaryCount")
        const label = findChild(widget, "mempoolSummaryInlineCountLabel")
        tryVerify(function() {
            return Math.abs(count.mapToItem(widget, 0, count.baselineOffset).y
                - label.mapToItem(widget, 0, label.baselineOffset).y) < 1
        })
        verify(label.mapToItem(widget, 0, 0).x >= count.mapToItem(widget, count.width, 0).x)
        compare(findChild(widget, "mempoolQueuedBlocksLabel").lineCount, 1)
        widget.columnSpan = 3
        widget.rowSpan = 2
        widget.width = 392
        widget.height = 244
        waitForPolish(widget)
        verify(findChild(widget, "mempoolSummaryDivider").visible)
        const fee = findChild(widget, "mempoolSummaryFee")
        const memory = findChild(widget, "mempoolSummaryMemory")
        compare(fee.font, memory.font)
        const originalSize = fee.font.pixelSize
        const table = findChild(widget, "mempoolSummaryTable")
        const countLabel = findChild(widget, "mempoolSummaryCountLabel")
        const blocksLabel = findChild(widget, "mempoolQueuedBlocksLabel")
        function labelsAligned() {
            return Math.abs(countLabel.mapToItem(widget, 0, countLabel.baselineOffset).y
                - blocksLabel.mapToItem(widget, 0, blocksLabel.baselineOffset).y) < 1
        }
        tryVerify(labelsAligned)
        compare(count.font.pixelSize, widget.scaledPixelSize(Theme.text.widgetPrimaryValueLarge))
        const tableArea = findChild(widget, "mempoolSummaryTableArea")
        tryVerify(function() { return Math.abs(table.height - Math.max(table.implicitHeight, tableArea.height * 0.75)) < 1 })
        const originalTableHeight = table.height
        widget.width = 992
        widget.height = 644
        waitForPolish(widget)
        compare(fee.font.pixelSize, originalSize)
        compare(fee.font, memory.font)
        tryVerify(function() { return !fee.truncated && !memory.truncated })
        tryVerify(labelsAligned)
        tryVerify(function() { return table.height > originalTableHeight * 2 })
        tryVerify(function() { return Math.abs(table.mapToItem(widget, 0, table.height).y - widget.height) < 1 })
        tryVerify(function() { return Math.abs(table.height - tableArea.height * 0.75) < 1 })
    }

    function test_zeroUnavailableAndIndependentVisibility() {
        const widget = createTemporaryObject(fixture, testCase, {columnSpan: 3, rowSpan: 2, width: 392, height: 244})
        compare(activity.summaryActive, true)
        activity.queuedVbytes = 0
        compare(widget.queuedBlocks, 0)
        activity.queuedVbytes = -1
        compare(widget.ready, false)
        compare(findChild(widget, "mempoolSummaryCount").text, "—")
        node.mempoolInformationAvailable = false
        compare(activity.summaryActive, false)
        widget.destroy()
        wait(0)
        compare(activity.active, true)
    }
}

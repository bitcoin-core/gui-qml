// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
import QtQuick 2.15
import QtTest 1.2
import "../../qml/components/widgets"
import "../../qml/controls"

TestCase {
    id: testCase
    name: "HalvingWidget"
    when: windowShown
    visible: true
    width: 600
    height: 450
    QtObject {
        id: milestone
        property bool available: true
        property bool complete: false
        property real progress: 0.6139
        property int blocksLeft: 81067
        property int periodStart: 840000
        property int nextHeight: 1050000
        property real secondsRemaining: 81067 * 600
        property real subsidy: 3.125
        property real nextSubsidy: 1.5625
    }
    Component { id: fixture; HalvingWidget { milestoneModel: milestone } }
    function init() { milestone.progress = 0.6139; milestone.available = true; milestone.complete = false; milestone.subsidy = 3.125; milestone.nextSubsidy = 1.5625 }
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
        compare(widget.percent, 61)
        for (const name of ["halvingHeadline", "halvingBlocksRemaining", "halvingArrival", "halvingSubsidy", "halvingProgressBar"]) {
            const item = findChild(widget, name)
            if (!item.visible) continue
            const point = item.mapToItem(widget, 0, 0)
            verify(point.x >= 0 && point.y >= 0, name)
            verify(point.x + item.width <= widget.width + 1, name)
            verify(point.y + item.height <= widget.height + 1, name)
            if (item.truncated !== undefined) verify(!item.truncated, name)
        }
        const headline = findChild(widget, "halvingHeadline")
        compare(headline.text, "61%")
        compare(headline.mapToItem(widget, 0, 0).x, 0)
        if (!widget.compact) {
            const blocks = findChild(widget, "halvingBlocksRemaining")
            verify(headline.font.pixelSize > blocks.font.pixelSize)
            verify(blocks.mapToItem(widget, 0, 0).x > headline.mapToItem(widget, 0, 0).x)
        }
        const track = findChild(widget, "halvingProgressBar")
        const fill = findChild(widget, "halvingProgressFill")
        fuzzyCompare(fill.width, track.width * milestone.progress, 0.01)
        milestone.progress = 0
        compare(fill.width, 0)
        milestone.progress = 1
        compare(fill.width, track.width)
        if (widget.expanded) {
            const section = findChild(widget, "halvingDetailsSection")
            fuzzyCompare(section.height, section.implicitHeight * 1.4, 1)
        }
    }
    function test_unavailableAndFinalSatoshi() {
        const widget = createTemporaryObject(fixture, testCase, {columnSpan: 3, rowSpan: 2, width: 392, height: 244})
        compare(widget.subsidy(0.00000001), "0.00000001")
        milestone.subsidy = 0.00000001
        milestone.nextSubsidy = 0
        compare(findChild(widget, "halvingSubsidy").text, "0.00000001 → 0")
        milestone.complete = true
        compare(widget.arrival(), "Subsidy complete")
        milestone.available = false
        compare(findChild(widget, "halvingHeadline").text, "—")
        compare(widget.arrival(), "—")
    }
}

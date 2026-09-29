// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtTest 1.2
import "../../qml/components/widgets"
import "../../qml/controls"

TestCase {
    id: testCase
    name: "DifficultyPeriodWidget"
    when: windowShown
    visible: true
    width: 620
    height: 460

    QtObject {
        id: period
        property bool ready: true
        property bool available: true
        property bool active: false
        property real progress: 1253 / 2016
        property real nextChange: -1.89
        property real previousChange: 4.16
        property int blocksLeft: 763
        property real averageBlockSeconds: 612
    }
    Component {
        id: fixture
        DifficultyPeriodWidget {
            periodModel: period
            Rectangle { anchors.fill: parent; z: -1; color: Theme.color.background }
        }
    }
    Component { id: registryFixture; DefaultWidgetRegistry {} }
    function init() { period.available = true; period.progress = 1253 / 2016; Theme.dark = true; period.averageBlockSeconds = 612; period.blocksLeft = 763 }
    function cleanup() { Theme.dark = true }

    function test_layout_data() {
        return [
            {tag: "one-cell", width: 112, height: 112, columns: 1, rows: 1, dark: true},
            {tag: "paper-small", width: 260, height: 112, columns: 2, rows: 1, dark: true},
            {tag: "paper-medium", width: 392, height: 244, columns: 3, rows: 2, dark: true},
            {tag: "small-dark", width: 330, height: 148, columns: 2, rows: 1, dark: true},
            {tag: "small-light", width: 330, height: 148, columns: 2, rows: 1, dark: false},
            {tag: "medium-constrained", width: 400, height: 220, columns: 3, rows: 2, dark: true},
            {tag: "portrait-small", width: 275, height: 115, columns: 2, rows: 1, dark: true},
            {tag: "medium-dark", width: 520, height: 330, columns: 3, rows: 2, dark: true},
            {tag: "medium-light", width: 520, height: 330, columns: 3, rows: 2, dark: false}
        ]
    }
    function test_layout(data) {
        Theme.dark = data.dark
        const widget = createTemporaryObject(fixture, testCase, {
            width: data.width, height: data.height, columnSpan: data.columns, rowSpan: data.rows
        })
        waitForRendering(widget)
        waitForPolish(widget)
        compare(findChild(widget, "difficultyProgress").text, "62%")
        compare(findChild(widget, "difficultyAverageBlockTime").text, "10m 12s")
        compare(findChild(widget, "difficultyAverageBlockTime").horizontalAlignment, Text.AlignRight)
        const bar = findChild(widget, "difficultyProgressBar")
        const next = findChild(widget, "difficultyNextChange")
        const previous = findChild(widget, "difficultyPreviousChange")
        const section = findChild(widget, "difficultyProgressSection")
        compare(section.background.color, Theme.color.neutral1)
        if (widget.expanded) {
            compare(previous.font.pixelSize, widget.scaledPixelSize(Theme.text.captionStrong) * 2)
            verify(next.font.pixelSize > widget.scaledPixelSize(Theme.text.widgetDisplay))
            for (const pair of [["difficultyNextChange", "difficultyNextLabel"], ["difficultyPreviousChange", "difficultyPreviousLabel"]]) {
                const number = findChild(widget, pair[0])
                const caption = findChild(widget, pair[1])
                verify(caption.mapToItem(widget, 0, 0).y >= number.mapToItem(widget, 0, number.height).y)
            }
            verify(Math.abs(section.height - section.implicitHeight * 1.2) < 1)
        }
        verify(bar.width <= widget.width)
        verify(bar.mapToItem(widget, 0, 0).y > next.mapToItem(widget, 0, next.height).y)
        compare(findChild(widget, "difficultyTimeRemaining").text, "In ≈5d 9h")
        const names = ["difficultyProgress", "difficultyNextChange", "difficultyPreviousChange", "difficultyBlocksLeft", "difficultyCompactBlocksLeft", "difficultyAverageBlockTime", "difficultyTimeRemaining", "difficultyAverageLabel", "difficultyPreviousLabel", "difficultyNextLabel"]
        for (const name of names) {
            const label = findChild(widget, name)
            if (!label.visible) continue
            const point = label.mapToItem(widget, 0, 0)
            verify(point.x >= 0 && point.y >= 0, name)
            verify(point.x + label.width <= widget.width + 1, name)
            verify(point.y + label.height <= widget.height + 1, name)
            verify(label.contentWidth <= label.width + 1, name)
            compare(label.font.family, Theme.text.family)
            for (const otherName of names) {
                if (name === otherName) continue
                const other = findChild(widget, otherName)
                if (!other.visible) continue
                const otherPoint = other.mapToItem(widget, 0, 0)
                verify(point.x + label.width <= otherPoint.x + 1 || otherPoint.x + other.width <= point.x + 1
                    || point.y + label.height <= otherPoint.y + 1 || otherPoint.y + other.height <= point.y + 1,
                    name + " overlaps " + otherName)
            }
        }
    }
    function test_remainingTime() {
        const widget = createTemporaryObject(fixture, testCase, {width: 330, height: 148, columnSpan: 2, rowSpan: 1})
        const remaining = findChild(widget, "difficultyTimeRemaining")
        period.blocksLeft = 10
        compare(remaining.text, "In ≈1h 42m")
        period.blocksLeft = 1
        compare(remaining.text, "In ≈11m")
        period.averageBlockSeconds = NaN
        compare(remaining.text, "—")
    }
    function test_compactBlocksLeft() {
        const widget = createTemporaryObject(fixture, testCase, {width: 112, height: 112})
        const blocks = findChild(widget, "difficultyCompactBlocksLeft")
        const remaining = findChild(widget, "difficultyTimeRemaining")
        period.blocksLeft = 2000
        compare(blocks.text, "2000 left")
        compare(blocks.visible, true)
        compare(remaining.visible, false)
        period.available = false
        compare(blocks.text, "—")
    }
    function test_floorUnavailableAndRemoval() {
        const widget = createTemporaryObject(fixture, testCase, {width: 330, height: 148})
        period.progress = 0.9999
        compare(findChild(widget, "difficultyProgress").text, "99%")
        period.available = false
        compare(findChild(widget, "difficultyNextChange").text, "—")
        compare(findChild(widget, "difficultyAverageBlockTime").text, "—")
        compare(period.active, true)
        widget.destroy()
        wait(0)
        compare(period.active, false)
        const registry = createTemporaryObject(registryFixture, testCase)
        compare(registry.catalog[3].id, "difficulty-period")
        compare(registry.catalog[3].sizes.map(function(s) { return {columns: s.columns, rows: s.rows} }), [{columns: 2, rows: 1}, {columns: 3, rows: 2}, {columns: 1, rows: 1}])
    }
}

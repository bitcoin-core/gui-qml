// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtTest 1.2
import "../../qml/controls"

TestCase {
    id: testCase
    name: "PieChart"
    when: windowShown
    visible: true
    width: 300; height: 220
    Component {
        id: fixtureComponent
        Rectangle {
            width: 200; height: 200; color: "black"
            property alias chart: chart
            PieChart { id: chart; anchors.fill: parent; gap: 0 }
        }
    }
    SignalSpy { id: paintSpy; signalName: "painted" }
    function render(fixture) {
        paintSpy.target = fixture.chart
        paintSpy.clear()
        fixture.chart.requestPaint()
        if (fixture.chart.active) paintSpy.wait()
        wait(0)
        return grabImage(fixture)
    }
    function cleanup() { Theme.dark = true }
    function test_pieAndTransparentDonut() {
        const fixture = createTemporaryObject(fixtureComponent, testCase)
        fixture.chart.slices = [{value: 1, color: "red"}, {value: 1, color: "blue"}]
        let image = render(fixture)
        verify(image.red(150, 100) > 200)
        verify(image.blue(50, 100) > 200)
        verify(image.red(110, 100) > 200)
        fixture.chart.style = PieChart.Donut
        image = render(fixture)
        compare(image.red(110, 100), 0)
        verify(image.red(185, 100) > 200)
        verify(image.blue(15, 100) > 200)
        fixture.color = "lime"
        image = render(fixture)
        verify(image.green(100, 100) > 200)
        fixture.chart.slices = [{value: 1, color: "rgba(255, 0, 0, 0.5)"}]
        image = render(fixture)
        verify(image.green(100, 100) > 200) // Slice alpha must not affect the hole.
    }
    function test_invalidValuesAndStableColors() {
        const fixture = createTemporaryObject(fixtureComponent, testCase)
        fixture.chart.colors = ["red", "blue", "yellow"]
        fixture.chart.slices = [{value: 0}, {value: 2}, {value: NaN}, null,
                               {value: -1}, {value: Infinity}, {value: "3"}]
        compare(fixture.chart.segments.length, 1)
        compare(fixture.chart.segments[0].color, "blue")
        compare(fixture.chart.total, 2)
        verify(render(fixture).blue(100, 100) > 200)
        fixture.chart.slices = []
        compare(fixture.chart.hasSlices, false)
        compare(fixture.chart.total, 0)
        compare(render(fixture).blue(100, 100), 0)
        fixture.chart.slices = [{value: 1e308}, {value: 1e308}]
        compare(fixture.chart.segments[0].fraction, 0.5)
        compare(fixture.chart.segments[1].fraction, 0.5)
    }
    function test_singleSliceGapAndRectangularBounds() {
        const fixture = createTemporaryObject(fixtureComponent, testCase, {width: 260, height: 120})
        fixture.chart.slices = [{value: 1, color: "red"}]
        fixture.chart.style = PieChart.Donut
        fixture.chart.gap = 20
        let image = render(fixture)
        verify(image.red(130, 8) > 200)
        compare(image.red(30, 60), 0)
        compare(image.red(130, 60), 0) // A single slice must keep its donut hole.
        fixture.chart.slices = [{value: 1, color: "red"}, {value: 1, color: "blue"}]
        image = render(fixture)
        compare(image.red(130, 8), 0)
        compare(image.blue(130, 8), 0)
        fixture.chart.holeRatio = 2
        compare(fixture.chart.innerRatio, 0.95)
        fixture.chart.holeRatio = -1
        compare(fixture.chart.innerRatio, 0)
        fixture.width = 1; fixture.height = 1
        render(fixture)
    }
    function test_themeAndInactiveUpdates() {
        const fixture = createTemporaryObject(fixtureComponent, testCase)
        fixture.chart.slices = [{value: 1}]
        compare(fixture.chart.segments[0].color, Theme.color.orange)
        const dark = render(fixture)
        fixture.chart.active = false
        Theme.dark = false
        compare(fixture.chart.segments[0].color, Theme.color.orange)
        verify(render(fixture).equals(dark))
        fixture.chart.active = true
        verify(!render(fixture).equals(dark))
    }

    function test_inactiveAndHiddenUpdatesDoNotSchedulePaints() {
        const fixture = createTemporaryObject(fixtureComponent, testCase)
        const chart = fixture.chart
        chart.slices = [{value: 1, color: "red"}]
        render(fixture)
        wait(50)
        chart.active = false
        paintSpy.clear()
        chart.slices = [{value: 1, color: "blue"}]
        chart.gap = 4
        chart.requestPaint()
        wait(50)
        compare(paintSpy.count, 0)
        chart.active = true
        verify(render(fixture).blue(100, 100) > 200)
        chart.visible = false
        paintSpy.clear()
        chart.slices = [{value: 1, color: "red"}]
        chart.requestPaint()
        wait(50)
        compare(paintSpy.count, 0)
        chart.visible = true
        verify(render(fixture).red(100, 100) > 200)
    }
}

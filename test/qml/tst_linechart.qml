// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtTest 1.2
import "../../qml/controls"

TestCase {
    id: testCase
    name: "LineChart"
    when: windowShown
    width: 300
    height: 180
    visible: true

    Component {
        id: chartComponent
        Rectangle {
            width: 200
            height: 100
            color: "black"
            property alias chart: chart
            LineChart {
                id: chart
                anchors.fill: parent
                plotPadding: 10
                xMinimum: 0
                xMaximum: 10
                yMinimum: 0
                yMaximum: 10
                lineColor: "white"
                lineWidth: 4
            }
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

    function test_missingSamplesAndLargeGapsNeverConnect() {
        const fixture = createTemporaryObject(chartComponent, testCase)
        fixture.chart.points = [{x: 0, y: 5}, {x: 4, y: 5}, {x: 5, y: null}, {x: 6, y: 5}, {x: 10, y: 5}]
        let image = render(fixture)
        verify(image.red(50, 50) > 200)
        compare(image.red(100, 50), 0)
        fixture.chart.points = [{x: 0, y: 5}, {x: 4, y: 5}, {x: 6, y: 5}, {x: 10, y: 5}]
        image = render(fixture)
        verify(image.red(100, 50) > 200)
        fixture.chart.maximumGap = 1
        image = render(fixture)
        compare(image.red(100, 50), 0)
        fixture.chart.points = [{x: 0, y: 5}, {x: 5, y: NaN}, {x: 10, y: 5}]
        image = render(fixture)
        compare(image.red(100, 50), 0)
    }

    function test_smoothCurveMarkersAndGradient() {
        const fixture = createTemporaryObject(chartComponent, testCase)
        fixture.chart.points = [{x: 0, y: 0, color: "red"}, {x: 10, y: 10, color: "blue"}]
        fixture.chart.showPoints = true
        fixture.chart.pointRadius = 4
        fixture.chart.gradientStops = [{position: 0, color: "red"}, {position: 1, color: "blue"}]
        const straight = render(fixture)
        verify(straight.red(10, 90) > 200)
        verify(straight.blue(190, 10) > 200)
        verify(straight.red(100, 50) > 80 && straight.blue(100, 50) > 80)
        fixture.chart.smooth = true
        const smooth = render(fixture)
        verify(!smooth.equals(straight))
        verify(smooth.red(10, 90) > 200)
        verify(smooth.blue(190, 10) > 200)
    }

    function test_referenceLineFillAndInactiveUpdates() {
        const fixture = createTemporaryObject(chartComponent, testCase)
        fixture.chart.points = [{x: 0, y: 5}, {x: 10, y: 5}]
        fixture.chart.fillOpacity = 0.5
        fixture.chart.referenceLines = [{value: 2.5, color: "red", lineWidth: 4}]
        let image = render(fixture)
        verify(image.red(100, 60) > 100 && image.red(100, 60) < 160)
        verify(image.red(100, 70) > 200)
        fixture.chart.active = false
        fixture.chart.points = [{x: 0, y: 8}, {x: 10, y: 8}]
        image = render(fixture)
        verify(image.red(100, 50) > 200)
        fixture.chart.active = true
        image = render(fixture)
        verify(image.red(100, 26) > 200)
    }

    function test_autoScaleEmptyAndResponsiveAxes() {
        const fixture = createTemporaryObject(chartComponent, testCase)
        fixture.chart.xMinimum = Qt.binding(function() { return fixture.chart.dataRange.minX })
        fixture.chart.xMaximum = Qt.binding(function() { return Math.max(fixture.chart.xMinimum + 1, fixture.chart.dataRange.maxX) })
        fixture.chart.yMaximum = Qt.binding(function() { return Math.max(1, fixture.chart.dataRange.maxY) })
        fixture.chart.points = [{x: 1000, y: 4}, {x: 6000, y: 8}]
        compare(fixture.chart.xMinimum, 1000)
        compare(fixture.chart.xMaximum, 6000)
        compare(fixture.chart.yMaximum, 8)
        fixture.chart.showYAxis = true
        fixture.chart.xLabels = [{value: 1000, text: "Start"}, {value: 6000, text: "End"}]
        render(fixture)
        verify(fixture.chart.plotHeight < fixture.height)
        compare(fixture.chart.yLabelCount, 3)
        fixture.height = 50
        render(fixture)
        compare(fixture.chart.yLabelCount, 2)
        fixture.chart.points = []
        render(fixture)
        compare(fixture.chart.hasPoints, false)
        verify(fixture.chart.xMaximum > fixture.chart.xMinimum)
        verify(fixture.chart.yMaximum > fixture.chart.yMinimum)
        fixture.chart.showYAxis = false
        fixture.chart.showXAxis = false
        fixture.width = 4
        fixture.height = 4
        render(fixture)
    }

    function test_inactiveAndHiddenUpdatesDoNotSchedulePaints() {
        const fixture = createTemporaryObject(chartComponent, testCase)
        const chart = fixture.chart
        chart.points = [{x: 0, y: 5}, {x: 10, y: 5}]
        render(fixture)
        wait(50)
        compare(chart.selectionSeries.length, 0)
        chart.interactive = true
        compare(chart.selectionSeries[0].length, 2)
        chart.active = false
        paintSpy.clear()
        chart.points = [{x: 0, y: 8}, {x: 10, y: 8}]
        chart.lineWidth = 5
        chart.requestPaint()
        wait(50)
        compare(paintSpy.count, 0)
        compare(chart.selectionSeries.length, 0)
        chart.active = true
        verify(render(fixture).red(100, 26) > 200)

        chart.visible = false
        paintSpy.clear()
        chart.points = [{x: 0, y: 2}, {x: 10, y: 2}]
        chart.requestPaint()
        wait(50)
        compare(paintSpy.count, 0)
        chart.visible = true
        verify(render(fixture).red(100, 74) > 200)
    }
    function test_selectionMatchesFractionalDomains() {
        const fixture = createTemporaryObject(chartComponent, testCase)
        const chart = fixture.chart
        chart.interactive = true
        chart.xMaximum = 0.5
        chart.yMaximum = 0.0000005
        chart.points = [{x: 0, y: 0}, {x: 0.5, y: 0.0000005}]
        render(fixture)
        compare(chart.pixelX(0.5), 190)
        compare(chart.pixelY(0.0000005), 10)
        chart.selectAt(chart.pixelX(0.5))
        compare(chart.selectedX, 0.5)
    }

    function test_multipleSeriesAndSharedRange() {
        const fixture = createTemporaryObject(chartComponent, testCase)
        fixture.chart.series = [
            {points: [{x: 0, y: 3}, {x: 10, y: 3}], lineColor: "red"},
            {points: [{x: 0, y: 7}, {x: 10, y: 7}], lineColor: "blue", lineDashPattern: [8, 4]}
        ]
        const image = render(fixture)
        verify(image.red(50, 66) > 200)
        verify(image.blue(20, 34) > 200)
        compare(fixture.chart.dataRange.minY, 3)
        compare(fixture.chart.dataRange.maxY, 7)
    }

    function test_scrubbingSnapsWithoutRepaintingAndPreservesGaps() {
        const fixture = createTemporaryObject(chartComponent, testCase)
        const chart = fixture.chart
        chart.series = [
            {points: [{x: 0, y: 3}, {x: 5, y: 4}, {x: 10, y: 3}], lineColor: "red"},
            {points: [{x: 0, y: 7}, {x: 5, y: null}, {x: 10, y: 7}], lineColor: "blue"}
        ]
        chart.interactive = true
        render(fixture)
        paintSpy.clear()
        const pointer = findChild(chart, "lineChartPointer")
        mouseMove(pointer, 95, 45)
        tryCompare(chart, "selectedX", 5)
        compare(chart.selectedPoints[0].y, 4)
        compare(chart.selectedPoints[1], null)
        compare(paintSpy.count, 0)
        verify(findChild(chart, "lineChartCursor").visible)
        chart.forceActiveFocus()
        keyClick(Qt.Key_Right)
        compare(chart.selectedX, 10)
        compare(chart.selectedPoints[1].y, 7)
        keyClick(Qt.Key_Escape)
        verify(!chart.selectionVisible)
        compare(chart.pointAt([{x: 0, y: 1}, {x: 10, y: 2}], 5, 4), null)
        compare(chart.pointAt([{x: 0, y: 1}], 10), null)
        chart.selectAt(10)
        chart.series = [{points: [{x: 5, y: 1}, {x: 10, y: 2}]}]
        compare(chart.selectedX, 0) // Selection stays at the original x when history advances.
        compare(chart.selectedPoints[0], null)
        chart.active = false
        verify(!chart.selectionVisible)
    }

    function test_touchScrubbingAndControlledSelection() {
        const fixture = createTemporaryObject(chartComponent, testCase)
        const chart = fixture.chart
        chart.points = [{x: 0, y: 1}, {x: 5, y: 2}, {x: 10, y: 3}]
        chart.interactive = true
        render(fixture)
        const pointer = findChild(chart, "lineChartPointer")
        const touch = touchEvent(pointer)
        touch.press(0, pointer, 12, 50).commit()
        tryCompare(chart, "selectedX", 0)
        touch.move(0, pointer, 185, 50).commit()
        tryCompare(chart, "selectedX", 10)
        touch.release(0, pointer, 185, 50).commit()
        chart.selectedX = Qt.binding(function() { return 5 })
        chart.selectAt(190)
        compare(chart.selectedX, 5)
        compare(chart.interactionX, 10)
        compare(chart.selectedPoints[0].y, 2)
    }

}

// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtTest 1.2
import "../../qml/components/widgets"
import "../../qml/controls"

RenderTestCase {
    id: testCase
    name: "NetworkTrafficWidget"
    when: windowShown
    visible: true
    width: 1200; height: 1000
    QtObject {
        id: traffic
        property bool widgetActive: false
        property bool active: false
        property var widgetHistory: []
        property real totalBytesReceived: 1240000000
        property real totalBytesSent: 486000000
    }
    Component {
        id: widgetComponent
        NetworkTrafficWidget {
            trafficModel: traffic
            Rectangle { anchors.fill: parent; z: -1; color: Theme.color.background }
        }
    }
    Component { id: registryComponent; DefaultWidgetRegistry {} }

    function init() {
        Theme.dark = true
        traffic.active = false
        traffic.widgetActive = false
        traffic.widgetHistory = [
            {time: 100000, received: 65000, sent: 25000},
            {time: 101000, received: 100000, sent: 13000},
            {time: 102000, received: 84200, sent: 12600}
        ]
    }
    function cleanup() { Theme.dark = true }
    function test_sizes_data() {
        const cases = []
        for (const cell of [136, 160, 240]) for (const size of [[1, 1], [2, 1], [3, 2]]) for (const dark of [true, false]) {
            cases.push({tag: cell + "-" + size.join("x") + "-" + dark, cell: cell, columns: size[0], rows: size[1], dark: dark})
        }
        return cases
    }
    function test_sizes(data) {
        Theme.dark = data.dark
        const widget = createTemporaryObject(widgetComponent, testCase, {
            width: data.columns * data.cell + (data.columns - 1) * 12 - 32,
            height: data.rows * data.cell + (data.rows - 1) * 12 - 32,
            columnSpan: data.columns, rowSpan: data.rows, cellSize: data.cell
        })
        waitForLayout(widget)
        waitForRendering(widget)
        compare(traffic.widgetActive, true)
        compare(traffic.active, false)
        const chart = findChild(widget, "networkTrafficWidgetChart")
        verify(chart instanceof LineChart)
        compare(chart.series.length, 2)
        compare(chart.series[0].lineColor, Theme.color.blue)
        compare(chart.series[1].lineColor, Theme.color.purple)
        compare(chart.lineWidth, 2)
        compare(chart.showXAxis, widget.expanded)
        compare(chart.showGrid, widget.expanded)
        compare(chart.yMaximum, 110000)
        const position = chart.mapToItem(widget, 0, 0)
        verify(chart.height >= 20)
        verify(position.x >= 0 && position.y >= 0)
        verify(position.x + chart.width <= widget.width + 1)
        verify(position.y + chart.height <= widget.height + 1)
        const total = findChild(widget, "networkTrafficWidgetTotals")
        compare(total.visible, widget.expanded)
        if (widget.expanded) {
            const bounds = total.mapToItem(widget, 0, 0)
            verify(bounds.y >= position.y + chart.height)
            verify(bounds.y + total.height <= widget.height + 1)
            compare(findChild(widget, "networkTrafficWidgetTotalReceived").text, "1.24 GB")
            compare(findChild(widget, "networkTrafficWidgetTotalSent").text, "486 MB")
        }
        const rate = findChild(widget, widget.compact ? "networkTrafficCompactReceived" : "networkTrafficReceivedRate")
        compare(rate.text, "84.2 KB/s")
        compare(rate.font, widget.primaryValueFont)
        verify(!rate.truncated)
        if (!widget.compact) {
            const sent = findChild(widget, "networkTrafficSentRate")
            compare(sent.text, "12.6 KB/s")
            verify(!sent.truncated)
            for (const reading of [rate, sent]) {
                const bounds = reading.mapToItem(widget, 0, 0)
                verify(bounds.x >= 0 && bounds.x + reading.width <= widget.width + 1, reading.objectName + " fits within the widget")
            }
            compare(findChild(widget, "networkTrafficReceivedLabel").text, "Received")
            compare(findChild(widget, "networkTrafficSentLabel").text, "Sent")
        }
        const title = findChild(widget, "networkTrafficWidgetTitle")
        compare(title.text, "Network traffic")
        compare(title.font, Theme.text.widgetTitle.font)
        verify(!title.truncated)
    }
    function test_emptyZeroAndUnits() {
        const widget = createTemporaryObject(widgetComponent, testCase, {width: 400, height: 252, columnSpan: 3, rowSpan: 2})
        compare(widget.formatRate(0).amount, "0")
        compare(widget.formatRate(1000).unit, "KB/s")
        compare(widget.formatRate(1000000).unit, "MB/s")
        compare(widget.formatRate(-1).amount, "—")
        traffic.widgetHistory = [{time: 1000, received: 0, sent: 0}]
        verify(widget.ready)
        verify(widget.maximumRate > 0)
        compare(widget.received.amount, "0")
        traffic.widgetHistory = []
        compare(widget.ready, false)
        compare(findChild(widget, "networkTrafficWidgetStatus").text, "Collecting traffic…")
        compare(findChild(widget, "networkTrafficReceivedRate").text, "— B/s")
        widget.trafficModel = null
        compare(findChild(widget, "networkTrafficWidgetStatus").text, "Waiting for node")
    }
    function test_fiveMinuteRollingWindow() {
        const widget = createTemporaryObject(widgetComponent, testCase, {width: 400, height: 252, columnSpan: 3, rowSpan: 2})
        const chart = findChild(widget, "networkTrafficWidgetChart")
        // Startup still uses a complete five-minute axis, with samples at its right edge.
        traffic.widgetHistory = [{time: 1000, received: 10, sent: 5}]
        compare(chart.xMaximum, 1000)
        compare(chart.xMaximum - chart.xMinimum, 300000)
        traffic.widgetHistory = [
            {time: 100000, received: 1000000, sent: 2000000},
            {time: 200000, received: 900000, sent: 1000000},
            {time: 300000, received: 80, sent: 20},
            {time: 600000, received: 40, sent: 10}
        ]
        compare(chart.xMinimum, 300000)
        compare(chart.xMaximum, 600000)
        compare(chart.series[0].points, [{x: 300000, y: 80}, {x: 600000, y: 40}])
        compare(chart.series[1].points, [{x: 300000, y: 20}, {x: 600000, y: 10}])
        compare(chart.yMaximum, 88) // Older spikes must not flatten current traffic.
        compare(chart.maximumGap, 3000)
        traffic.widgetHistory = traffic.widgetHistory.concat([{time: 601000, received: 20, sent: 5}])
        compare(chart.xMinimum, 301000)
        compare(chart.xMaximum, 601000)
        compare(chart.series[0].points, [{x: 600000, y: 40}, {x: 601000, y: 20}])
        compare(chart.series[1].points, [{x: 600000, y: 10}, {x: 601000, y: 5}])
        compare(chart.yMaximum, 44)
        compare(widget.received.amount, "20")
        compare(widget.sent.amount, "5")
    }
    function test_sharedActivityAndPreview() {
        const first = createTemporaryObject(widgetComponent, testCase, {width: 104, height: 104})
        const second = createTemporaryObject(widgetComponent, testCase, {width: 104, height: 104})
        compare(traffic.widgetActive, true)
        first.active = false
        compare(traffic.widgetActive, true)
        second.destroy()
        wait(0)
        compare(traffic.widgetActive, false)
        const preview = createTemporaryObject(widgetComponent, testCase, {width: 104, height: 104, preview: true})
        compare(traffic.widgetActive, false)
        first.active = true
        compare(traffic.widgetActive, true)
        first.destroy()
        wait(0)
        compare(traffic.widgetActive, false)
        verify(preview.preview)
    }
    function test_inactiveCopiesReleaseChartData() {
        const first = createTemporaryObject(widgetComponent, testCase, {width: 330, height: 148})
        const second = createTemporaryObject(widgetComponent, testCase, {width: 330, height: 148})
        first.active = false
        compare(traffic.widgetActive, true)
        compare(first.samples.length, 0)
        traffic.widgetHistory = [{time: 200000, received: 123, sent: 456}]
        compare(first.series[0].points.length, 0)
        compare(second.received.amount, "123")
        first.active = true
        compare(first.samples, traffic.widgetHistory)
        compare(first.received.amount, "123")

        const preview = createTemporaryObject(widgetComponent, testCase, {
            width: 330, height: 148, preview: true, active: false
        })
        compare(preview.samples, traffic.widgetHistory)
        preview.renderingActive = false
        compare(preview.samples.length, 0)
        traffic.widgetHistory = [{time: 201000, received: 789, sent: 10}]
        compare(preview.samples.length, 0)
        preview.renderingActive = true
        compare(preview.received.amount, "789")
    }
    function test_registry() {
        const registry = createTemporaryObject(registryComponent, testCase)
        const definitions = registry.catalog.filter(function(entry) { return entry.id === "network-traffic" })
        compare(definitions.length, 1)
        compare(definitions[0].sizes.map(function(size) { return [size.columns, size.rows] }), [[2, 1], [3, 2], [1, 1]])
    }
}

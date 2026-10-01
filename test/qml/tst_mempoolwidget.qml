// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtTest 1.2
import "../../qml/components/widgets"
import "../../qml/controls"

TestCase {
    id: testCase
    name: "MempoolWidget"
    when: windowShown
    width: 600
    height: 450
    visible: true
    QtObject {
        id: activity
        property var history: []
        property real baseline: 1000000 / 600
        property real incomingRate: 800
        property real minimumFee: 0.1
        property bool active: false
        property bool ready: true
    }
    QtObject {
        id: node
        property bool mempoolInformationAvailable: true
        property int mempoolTransactionCount: 518
        property real mempoolUsageMB: 1.86
        property real mempoolMaxUsageMB: 300
    }
    Component {
        id: widgetComponent
        MempoolWidget {
            activityModel: activity
            nodeModelRef: node
            Rectangle { anchors.fill: parent; z: -1; color: Theme.color.background }
        }
    }
    Component { id: registryComponent; DefaultWidgetRegistry {} }
    SignalSpy { id: paintSpy; signalName: "painted" }

    function init() {
        activity.history = [
            {time: 100000, rate: 300}, {time: 105000, rate: 200}, {time: 110000, rate: 3200},
            {time: 115000, rate: 1200}, {time: 120000, rate: 5000}, {time: 125000, rate: 500},
            {time: 130000, rate: 800}
        ]
        activity.ready = true
        activity.active = false
        node.mempoolInformationAvailable = true
        node.mempoolUsageMB = 1.86
        Theme.dark = true
    }
    function cleanup() { Theme.dark = true }

    function test_sizes_data() {
        return [
            {tag: "one-cell", width: 112, height: 112, columns: 1, rows: 1, dark: true},
            {tag: "paper-small", width: 260, height: 112, columns: 2, rows: 1, dark: true},
            {tag: "paper-medium", width: 392, height: 244, columns: 3, rows: 2, dark: true},
            {tag: "compact-dark", width: 330, height: 148, columns: 2, rows: 1, dark: true},
            {tag: "compact-light", width: 330, height: 148, columns: 2, rows: 1, dark: false},
            {tag: "portrait-compact", width: 275, height: 115, columns: 2, rows: 1, dark: true},
            {tag: "medium-constrained", width: 400, height: 220, columns: 3, rows: 2, dark: true},
            {tag: "expanded-dark", width: 520, height: 330, columns: 3, rows: 2, dark: true},
            {tag: "expanded-light", width: 520, height: 330, columns: 3, rows: 2, dark: false}
        ]
    }
    function test_sizes(data) {
        Theme.dark = data.dark
        const widget = createTemporaryObject(widgetComponent, testCase, {
            width: data.width, height: data.height, columnSpan: data.columns, rowSpan: data.rows
        })
        verify(widget !== null)
        waitForRendering(widget)
        waitForPolish(widget)
        const chart = findChild(widget, "incomingTransactionsChart")
        compare(chart.showAxes, !widget.compact)
        compare(chart.labelFont, widget.footerLabelFont)
        const rate = findChild(widget, "incomingTransactionsRate")
        compare(rate.visible, !widget.compact)
        if (!widget.compact) compare(rate.text, "800 vB/s")
        const position = chart.mapToItem(widget, 0, 0)
        verify(position.x >= 0 && position.y >= 0)
        verify(position.x + chart.width <= widget.width + 1)
        fuzzyCompare(position.y + chart.height, widget.height, 1)
        verify(chart.height > 0)
        if (!widget.compact) {
            const title = findChild(widget, "incomingTransactionsTitle")
            compare(chart.labelFont.pixelSize, widget.largeTypography ? Theme.text.widgetFooterLabelLarge.pixelSize : title.font.pixelSize)
            verify(position.y >= title.mapToItem(widget, 0, title.height).y + 8)
        }
        verify(findChild(widget, "incomingTransactionsLineChart") instanceof LineChart)
        const plot = findChild(widget, "incomingTransactionsPlot")
        paintSpy.target = plot
        plot.requestPaint()
        paintSpy.wait()
    }

    function test_colorsAndMissingData() {
        const widget = createTemporaryObject(widgetComponent, testCase, {width: 330, height: 148})
        const chart = findChild(widget, "incomingTransactionsChart")
        compare(chart.colorForRate(0), Theme.color.green)
        compare(chart.colorForRate(activity.baseline), Theme.color.green)
        compare(chart.colorForRate(activity.baseline * 2), Theme.color.blue)
        compare(chart.colorForRate(activity.baseline * 3), Theme.color.red)
        activity.history = []
        compare(findChild(widget, "mempoolChartStatus").text, "Collecting incoming transactions…")
        node.mempoolInformationAvailable = false
        compare(findChild(widget, "mempoolChartStatus").text, "Unavailable in blocks-only mode")
        compare(activity.active, false)
    }

    function test_registryAndRemoval() {
        const registry = createTemporaryObject(registryComponent, testCase)
        compare(registry.catalog[2].id, "mempool")
        compare(registry.catalog[2].sizes.map(function(s) { return {columns: s.columns, rows: s.rows} }), [{columns: 2, rows: 1}, {columns: 3, rows: 2}, {columns: 1, rows: 1}])
        const widget = createTemporaryObject(widgetComponent, testCase, {width: 330, height: 148})
        compare(activity.active, true)
        widget.destroy()
        wait(0)
        compare(activity.active, false)
    }

    function test_inactiveCopiesReleaseChartData() {
        const first = createTemporaryObject(widgetComponent, testCase, {width: 330, height: 148})
        const second = createTemporaryObject(widgetComponent, testCase, {width: 330, height: 148})
        first.active = false
        compare(activity.active, true)
        compare(first.samples.length, 0)
        activity.history = [{time: 200000, rate: 1234}]
        compare(first.samples.length, 0)
        compare(second.samples, activity.history)
        first.active = true
        compare(first.samples, activity.history)
    }
}

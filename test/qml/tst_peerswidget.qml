// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtTest 1.2
import "../../qml/components/widgets"
import "../../qml/controls"

TestCase {
    id: testCase
    name: "PeersWidget"
    when: windowShown
    visible: true
    width: 1200; height: 1000
    QtObject {
        id: peers
        property bool widgetActive: false
        property var summary: ({ready: false})
    }
    Component {
        id: widgetComponent
        PeersWidget {
            peerModel: peers
            Rectangle { anchors.fill: parent; z: -1; color: Theme.color.background }
        }
    }
    Component { id: registryComponent; DefaultWidgetRegistry {} }
    function init() {
        Theme.dark = true
        peers.widgetActive = false
        peers.summary = {ready: true, total: 18, inbound: 8, outbound: 10,
            groups: [{id: "ipv4", count: 9}, {id: "ipv6", count: 5}, {id: "tor", count: 3}, {id: "i2p", count: 1}]}
    }
    function cleanup() { Theme.dark = true }
    function test_sizes_data() {
        const cases = []
        for (const cell of [136, 160, 240]) for (const size of [[1, 1], [2, 1], [3, 2]]) for (const dark of [true, false])
            cases.push({tag: cell + "-" + size.join("x") + "-" + dark,
                cell: cell, columns: size[0], rows: size[1], dark: dark})
        return cases
    }
    function inBounds(widget, item) {
        const point = item.mapToItem(widget, 0, 0)
        verify(point.x >= -1 && point.y >= -1, item.objectName + " origin " + point.x + ", " + point.y)
        verify(point.x + item.width <= widget.width + 1, item.objectName + " right")
        verify(point.y + item.height <= widget.height + 1, item.objectName + " bottom")
    }
    function test_sizes(data) {
        Theme.dark = data.dark
        const widget = createTemporaryObject(widgetComponent, testCase, {
            width: data.columns * data.cell + (data.columns - 1) * 12 - 32,
            height: data.rows * data.cell + (data.rows - 1) * 12 - 32,
            columnSpan: data.columns, rowSpan: data.rows, cellSize: data.cell
        })
        waitForPolish(widget)
        waitForRendering(widget)
        compare(peers.widgetActive, true)
        compare(findChild(widget, "peersWidgetTotal").text, "18")
        compare(findChild(widget, "peersWidgetTitle").font, Theme.text.widgetTitle.font)
        const directions = findChild(widget, "peersWidgetDirections")
        inBounds(widget, directions)
        const metric = findChild(widget, "peersWidgetTotalLabel")
        verify(metric.mapToItem(widget, 0, metric.height).y <= directions.mapToItem(widget, 0, 0).y + 1
            , "Metric and directions overlap")
        for (const id of ["peersWidgetTitle", "peersWidgetTotal", "peersWidgetTotalLabel", "peersWidgetSplit"])
            inBounds(widget, findChild(widget, id))
        const inbound = findChild(widget, widget.compact ? "peersWidgetCompactInbound" : "peersWidgetInbound")
        const outbound = findChild(widget, widget.compact ? "peersWidgetCompactOutbound" : "peersWidgetOutbound")
        compare(inbound.text, widget.expanded ? "8" : "8 inbound")
        compare(outbound.text, widget.expanded ? "10" : "10 outbound")
        compare(inbound.color, Theme.color.green)
        compare(outbound.color, Theme.color.blue)
        verify(!inbound.truncated && !outbound.truncated)
        if (!widget.expanded) {
            compare(inbound.mapToItem(directions, 0, 0).x, 0)
            compare(outbound.mapToItem(directions, outbound.width, 0).x, directions.width)
            compare(inbound.horizontalAlignment, Text.AlignLeft)
            compare(outbound.horizontalAlignment, Text.AlignRight)
        }
        const types = findChild(widget, "peersWidgetTypes")
        compare(types.visible, !widget.compact)
        const section = findChild(widget, "peersWidgetDirectionSection")
        compare(section.background.color, Theme.color.neutral1)
        inBounds(widget, section)
        compare(findChild(widget, "peersWidgetTotalLabel").font, widget.primaryLabelFont)
        compare(inbound.font, widget.expanded ? widget.secondaryValueFont : widget.footerLabelFont)
        if (!widget.compact && !widget.expanded) {
            compare(types.width, widget.width * 0.4)
            compare(types.mapToItem(widget, types.width, 0).x, widget.width)
        }
        if (!widget.compact) {
            inBounds(widget, findChild(widget, "peersWidgetLegend"))
            compare(findChild(widget, "peersWidgetGroupLabel_ipv4").font, widget.expanded ? widget.footerLabelFont : widget.primaryLabelFont)
            compare(findChild(widget, "peersWidgetGroupCount_ipv4").font, widget.expanded ? widget.footerValueFont : widget.secondaryValueFont)
            compare(findChild(widget, "peersWidgetGroupDot_ipv4").visible, widget.expanded)
        }
        if (widget.expanded) {
            const chart = findChild(widget, "peersWidgetChart")
            compare(chart.style, PieChart.Donut)
            compare(chart.slices.length, 4)
            compare(chart.segments.map(function(slice) { return slice.color }),
                [Theme.color.orange, Theme.color.purple, Theme.color.red, Theme.color.neutral9])
            verify(chart.width > 60)
            verify(chart.height <= widget.height * 0.4 + 1)
            verify(chart.innerRatio > 0)
            const typesTitle = findChild(widget, "peersWidgetTypesTitle")
            const legend = findChild(widget, "peersWidgetLegend")
            compare(typesTitle.font, widget.footerLabelFont)
            compare(typesTitle.mapToItem(widget, 0, 0).y, findChild(widget, "peersWidgetTotal").mapToItem(widget, 0, 0).y)
            compare(chart.mapToItem(widget, 0, 0).y - typesTitle.mapToItem(widget, 0, typesTitle.height).y, 16)
            compare(legend.width, chart.width)
            compare(legend.mapToItem(widget, 0, 0).x, chart.mapToItem(widget, 0, 0).x)
            inBounds(widget, chart)
            inBounds(widget, findChild(widget, "peersWidgetLegend"))
            verify(chart.mapToItem(widget, 0, chart.height).y <= findChild(widget, "peersWidgetLegend").mapToItem(widget, 0, 0).y)
        }
        if (data.cell === 160)
            grabImage(widget).save("/tmp/peers-" + data.columns + "x" + data.rows + "-" + (data.dark ? "dark" : "light") + ".png")
    }
    function test_liveAndUnavailable() {
        const widget = createTemporaryObject(widgetComponent, testCase, {width: 400, height: 252, columnSpan: 3, rowSpan: 2})
        peers.summary = {ready: true, total: 0, inbound: 0, outbound: 0, groups: []}
        verify(widget.ready)
        compare(findChild(widget, "peersWidgetTotal").text, "0")
        compare(findChild(widget, "peersWidgetStatus").text, "No connections")
        compare(widget.inboundFraction, 0)
        peers.summary = {ready: false}
        verify(!widget.ready)
        compare(findChild(widget, "peersWidgetTotal").text, "—")
        compare(findChild(widget, "peersWidgetStatus").text, "Unavailable")
        peers.summary = {ready: true, total: 6, inbound: 6, outbound: 0,
            groups: ["ipv4", "ipv6", "tor", "i2p", "cjdns", "other"].map(function(id) { return {id: id, count: 1} })}
        waitForPolish(widget)
        compare(widget.inboundFraction, 1)
        compare(widget.groups.length, 6)
        compare(findChild(widget, "peersWidgetLegend").columns, 2)
        for (const group of widget.groups) {
            verify(group.color !== Theme.color.green && group.color !== Theme.color.blue)
            inBounds(widget, findChild(widget, "peersWidgetGroup_" + group.id))
        }
        widget.columnSpan = 2
        widget.rowSpan = 1
        widget.width = 252
        widget.height = 104
        waitForPolish(widget)
        waitForRendering(widget)
        inBounds(widget, findChild(widget, "peersWidgetLegend"))
        compare(findChild(widget, "peersWidgetLegend").columns, 3)
        for (const group of widget.groups) inBounds(widget, findChild(widget, "peersWidgetGroup_" + group.id))
        peers.summary = {ready: true, total: 1, inbound: 0, outbound: 1, groups: [{id: "tor", count: 1}]}
        compare(widget.groups[0].color, Theme.color.red) // Color doesn't depend on position.
        peers.summary = {ready: true, total: 1, inbound: 0, outbound: 1, groups: [{id: "i2p", count: 1}]}
        const darkColor = widget.groups[0].color
        Theme.dark = false
        compare(widget.groups[0].color, Theme.color.neutral9)
        verify(widget.groups[0].color !== darkColor)
        widget.peerModel = null
        verify(!widget.ready)
        compare(peers.widgetActive, false)
    }
    SignalSpy { id: paintSpy; signalName: "painted" }
    function test_singleNetworkStaysDonut() {
        peers.summary = {ready: true, total: 10, inbound: 0, outbound: 10, groups: [{id: "ipv4", count: 10}]}
        const widget = createTemporaryObject(widgetComponent, testCase,
            {width: 712, height: 460, columnSpan: 3, rowSpan: 2, cellSize: 240})
        const chart = findChild(widget, "peersWidgetChart")
        waitForPolish(widget)
        waitForRendering(widget)
        paintSpy.target = chart
        paintSpy.clear()
        chart.requestPaint()
        paintSpy.wait()
        const position = chart.mapToItem(widget, chart.width / 2, chart.height / 2)
        const image = grabImage(widget)
        compare(image.red(Math.round(position.x), Math.round(position.y)), 0)
        compare(image.green(Math.round(position.x), Math.round(position.y)), 0)
        compare(image.blue(Math.round(position.x), Math.round(position.y)), 0)
        verify(chart.height <= widget.height * 0.4)
        image.save("/tmp/peers-single-donut.png")
    }
    function test_largeResizeBounds() {
        const widget = createTemporaryObject(widgetComponent, testCase,
            {width: 400, height: 252, columnSpan: 3, rowSpan: 2, cellSize: 136})
        const chart = findChild(widget, "peersWidgetChart")
        const heading = findChild(widget, "peersWidgetTypesTitle")
        const legend = findChild(widget, "peersWidgetLegend")
        // Exercise wide/short and narrow/tall hosts, including live resize,
        // independently of the nominal dashboard cell proportions.
        for (const size of [[860, 480, 280], [340, 200, 136], [1000, 260, 320],
            [350, 520, 160], [712, 460, 240], [400, 252, 136]]) {
            widget.width = size[0]
            widget.height = size[1]
            widget.cellSize = size[2]
            for (const ids of [["ipv4"], ["ipv4", "ipv6", "tor", "i2p"],
                ["ipv4", "ipv6", "tor", "i2p", "cjdns", "other"], []]) {
                peers.summary = {ready: true, total: ids.length, inbound: 0, outbound: ids.length,
                    groups: ids.map(function(id) { return {id: id, count: 1} })}
                waitForPolish(widget)
                waitForRendering(widget)
                verify(widget.expanded)
                for (const item of [heading, chart, legend]) inBounds(widget, item)
                if (ids.length === 0) inBounds(widget, findChild(widget, "peersWidgetStatus"))
                verify(chart.height <= widget.height * 0.4 + 1)
                compare(chart.width, chart.height)
                compare(legend.width, chart.width)
                compare(heading.mapToItem(widget, 0, 0).y,
                    findChild(widget, "peersWidgetTotal").mapToItem(widget, 0, 0).y)
                compare(chart.mapToItem(widget, 0, 0).y - heading.mapToItem(widget, 0, heading.height).y, 16)
                verify(legend.mapToItem(widget, 0, 0).y >= chart.mapToItem(widget, 0, chart.height).y)
                for (const id of ids) inBounds(widget, findChild(widget, "peersWidgetGroup_" + id))
            }
        }
    }
    function test_sharedActivityAndPreview() {
        const first = createTemporaryObject(widgetComponent, testCase, {width: 104, height: 104})
        const second = createTemporaryObject(widgetComponent, testCase, {width: 104, height: 104})
        compare(peers.widgetActive, true)
        first.active = false
        compare(peers.widgetActive, true)
        second.destroy()
        wait(0)
        compare(peers.widgetActive, false)
        const preview = createTemporaryObject(widgetComponent, testCase, {width: 104, height: 104, preview: true})
        compare(peers.widgetActive, false)
        first.active = true
        compare(peers.widgetActive, true)
        first.destroy()
        wait(0)
        compare(peers.widgetActive, false)
        verify(preview.preview)
    }
    function test_largePreviewRendersWithoutPolling() {
        const widget = createTemporaryObject(widgetComponent, testCase, {
            width: 500, height: 330, columnSpan: 3, rowSpan: 2, preview: true, active: false
        })
        waitForPolish(widget)
        const chart = findChild(widget, "peersWidgetChart")
        compare(peers.widgetActive, false)
        verify(chart.active)
        paintSpy.target = chart
        paintSpy.clear()
        chart.requestPaint()
        paintSpy.wait()
        widget.renderingActive = false
        compare(chart.active, false)
        compare(peers.widgetActive, false)
    }

    function test_registry() {
        const registry = createTemporaryObject(registryComponent, testCase)
        const definitions = registry.catalog.filter(function(entry) { return entry.id === "peers" })
        compare(definitions.length, 1)
        compare(definitions[0].sizes.map(function(size) { return [size.columns, size.rows] }), [[2, 1], [3, 2], [1, 1]])
    }
}

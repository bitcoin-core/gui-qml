// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import "../../controls"

Item {
    id: root
    property var samples: []
    property font labelFont: Theme.text.caption.font
    readonly property real axisScale: labelFont.pixelSize / Theme.text.caption.pixelSize
    property real capacityBaseline: 1000000 / 600
    property bool expanded: false
    property bool showAxes: true
    property bool active: true
    readonly property var rateColors: Theme.color.incomingRateColors
    readonly property color gridColor: Theme.color.neutral3
    readonly property color baselineColor: Theme.color.neutral8
    readonly property real maximum: {
        let value = capacityBaseline * 1.25
        for (let i = 0; i < samples.length; ++i) value = Math.max(value, samples[i].rate * 1.1)
        return value
    }
    readonly property real startTime: samples.length ? samples[0].time : 0
    readonly property real endTime: samples.length ? samples[samples.length - 1].time : 0
    readonly property bool hasSamples: samples.some(function(point) { return point.rate >= 0 })
    readonly property int axisLabelCount: chart.yLabelCount
    objectName: "incomingTransactionsChart"

    function colorForRate(rate) {
        const level = Math.max(0, Math.min(2, rate / capacityBaseline - 1))
        const lower = Math.floor(level)
        const fraction = level - lower
        const a = rateColors[lower]
        const b = rateColors[Math.min(2, lower + 1)]
        return Qt.rgba(a.r + (b.r - a.r) * fraction, a.g + (b.g - a.g) * fraction,
                       a.b + (b.b - a.b) * fraction, 1)
    }
    function axisLabel(rate) {
        if (rate >= 1000000) return qsTr("%1M").arg(Number(rate / 1000000).toLocaleString(Qt.locale(), 'f', 1))
        if (rate >= 1000) return qsTr("%1k").arg(Number(rate / 1000).toLocaleString(Qt.locale(), 'f', 1))
        return Number(rate).toLocaleString(Qt.locale(), 'f', 0)
    }
    function timeLabel(time) { return Qt.formatTime(new Date(time), "hh:mm") }
    function repaint() { chart.requestPaint() }

    readonly property var lineGradientStops: {
        const stops = [{position: 0, color: colorForRate(maximum)}]
        for (let i = 3; i >= 1; --i) {
            const position = 1 - capacityBaseline * i / maximum
            if (position > 0 && position < 1) stops.push({position: position, color: rateColors[i - 1]})
        }
        stops.push({position: 1, color: rateColors[0]})
        return stops
    }

    LineChart {
        id: chart
        objectName: "incomingTransactionsLineChart"
        plotObjectName: "incomingTransactionsPlot"
        anchors.fill: parent
        active: root.active
        points: root.samples.map(function(sample) {
            return {x: sample.time, y: sample.rate >= 0 ? sample.rate : null}
        })
        xMinimum: root.startTime
        xMaximum: root.startTime + Math.max(5000, root.endTime - root.startTime)
        yMinimum: 0
        yMaximum: root.maximum
        lineWidth: root.expanded ? 3 : 2
        showSegmentStarts: true
        maximumGap: 15000
        showGrid: true
        gridColor: root.gridColor
        verticalGradient: true
        gradientStops: root.lineGradientStops
        referenceLines: [{value: root.capacityBaseline, color: root.baselineColor,
                          lineWidth: 1.5, dashPattern: [5, 4]}]
        showYAxis: root.showAxes
        showXAxis: root.showAxes
        labelFont: root.labelFont
        axisScale: root.axisScale
        yLabelFormatter: function(value) { return root.axisLabel(value) }
        xLabels: {
            if (!root.hasSamples) return []
            const labels = [{value: root.startTime, text: root.timeLabel(root.startTime)}]
            if (root.endTime - root.startTime >= 60000)
                labels.push({value: root.endTime, text: root.timeLabel(root.endTime)})
            return labels
        }
    }
}

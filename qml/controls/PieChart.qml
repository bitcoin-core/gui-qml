// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15

// Slices are {value, color?, label?}. Zero, negative and non-finite values are
// omitted; automatic colors keep their original index when a slice disappears.
Item {
    id: root
    enum ChartStyle { Pie, Donut }
    property int style: PieChart.Pie
    property var slices: []
    property var colors: [Theme.color.orange, Theme.color.purple, Theme.color.red,
                          Theme.color.neutral9, Theme.color.amber, Theme.color.lavender]
    property real holeRatio: 0.72
    property real gap: 2
    property real startAngle: -90
    property bool active: true
    readonly property real innerRatio: style === PieChart.Donut
        ? Math.max(0, Math.min(0.95, isFinite(holeRatio) ? holeRatio : 0.72)) : 0
    readonly property var segments: {
        const values = []
        let maximum = 0
        for (let i = 0; i < slices.length; ++i) {
            const slice = slices[i]
            if (!slice || typeof slice.value !== "number" || !isFinite(slice.value) || slice.value <= 0) continue
            maximum = Math.max(maximum, slice.value)
            values.push({value: slice.value, label: slice.label || "",
                color: slice.color !== undefined ? slice.color
                    : colors.length ? colors[i % colors.length] : Theme.color.neutral9})
        }
        // Normalize before summing, so even very large finite values can draw.
        let sum = 0
        for (const value of values) sum += value.value / maximum
        for (const value of values) value.fraction = (value.value / maximum) / sum
        return values
    }
    readonly property bool hasSlices: segments.length > 0
    readonly property real total: segments.reduce(function(sum, slice) { return sum + slice.value }, 0)
    signal painted()
    function requestPaint() { plot.requestPaint() }
    onSegmentsChanged: requestPaint()
    onInnerRatioChanged: requestPaint()
    onGapChanged: requestPaint()
    onStartAngleChanged: requestPaint()
    onActiveChanged: if (active) requestPaint()

    Canvas {
        id: plot
        anchors.fill: parent
        antialiasing: true
        onAvailableChanged: if (available) requestPaint()
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onVisibleChanged: if (visible) requestPaint()
        onPainted: root.painted()
        onPaint: {
            if (!available || !root.active) return
            const ctx = getContext("2d")
            ctx.reset()
            ctx.clearRect(0, 0, width, height)
            const radius = Math.max(0, Math.min(width, height) / 2 - 1)
            if (!root.hasSlices || radius <= 0) return
            const cx = width / 2, cy = height / 2
            const inner = radius * root.innerRatio
            const requestedGap = isFinite(root.gap) ? Math.max(0, root.gap) : 0
            let angle = (isFinite(root.startAngle) ? root.startAngle : -90) * Math.PI / 180
            for (const segment of root.segments) {
                const sweep = segment.fraction * 2 * Math.PI
                // Keep tiny slices visible and never cut a gap into one full slice.
                const inset = root.segments.length > 1 ? Math.min(requestedGap / (2 * radius), sweep / 4) : 0
                const from = angle + inset, to = angle + sweep - inset
                ctx.beginPath()
                ctx.arc(cx, cy, radius, from, to, false)
                ctx.lineTo(cx, cy)
                ctx.closePath()
                ctx.fillStyle = segment.color
                ctx.fill()
                angle += sweep
            }
            if (inner > 0) {
                // Punch out the center after filling sectors. Qt Canvas can
                // collapse a reverse full-circle arc for a single donut slice.
                ctx.globalCompositeOperation = "destination-out"
                ctx.fillStyle = "black"
                ctx.beginPath()
                ctx.arc(cx, cy, inner, 0, 2 * Math.PI, false)
                ctx.fill()
                ctx.globalCompositeOperation = "source-over"
            }
        }
    }
}

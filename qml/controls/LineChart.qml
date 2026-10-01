// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15

// Points are ordered by x: {x, y, color?}; a null/non-finite y breaks the line.
Item {
    id: root

    property var points: []
    // Each series supplies points and may override the rendering properties below.
    property var series: []
    readonly property var plottedSeries: series.length ? series : [{points: points}]
    property var lineDashPattern: []
    property bool interactive: false
    // Override this binding to synchronize selection between charts; handle scrubbed.
    property real selectedX: interactionX
    property real interactionX: NaN
    readonly property bool scrubbing: pointer.pressed || activeFocus
    readonly property bool selectionVisible: interactive && active && isFinite(selectedX)
        && selectedX >= xMinimum && selectedX <= xMaximum
    readonly property var selectionSeries: interactive && active && visible ? plottedSeries.map(function(source) {
        return (source.points || []).filter(function(point) {
            return point && typeof point.x === "number" && isFinite(point.x)
        })
    }) : []
    readonly property var selectedPoints: selectionSeries.map(function(values, index) {
        return root.selectionVisible ? root.pointAt(values, root.selectedX,
            root.option(root.plottedSeries[index], "maximumGap")) : null
    })
    readonly property real selectedPixelX: plot.x + pixelX(selectedX)
    property color cursorColor: Theme.color.neutral6
    signal scrubbed(real value)
    property bool active: true
    property bool smooth: false
    property bool showPoints: false
    property bool showSegmentStarts: false
    property real maximumGap: Infinity
    property real lineWidth: 2
    property real pointRadius: 2
    property real plotPadding: 3
    property color lineColor: Theme.color.neutral9
    // Stops are {position: 0..1, color}, measured left-to-right or top-to-bottom.
    property var gradientStops: []
    property bool verticalGradient: false
    property real fillOpacity: 0
    property bool showGrid: false
    property int gridDivisions: 2
    property color gridColor: Theme.color.neutral3
    property var gridDashPattern: [2, 3]
    // Reference lines are {value, color, lineWidth?, dashPattern?}.
    property var referenceLines: []
    property bool showYAxis: false
    property var xLabels: []
    property bool showXAxis: xLabels.length > 0
    property font labelFont: Theme.text.caption.font
    property color labelColor: Theme.color.neutral7
    property real axisScale: labelFont.pixelSize / 13
    property real leftInset: showYAxis ? 40 * axisScale : 0
    property real bottomInset: showXAxis ? 20 * axisScale : 0
    property var yLabelFormatter: function(value) { return String(value) }
    property string plotObjectName: ""

    readonly property var dataRange: {
        let minX = Infinity, maxX = -Infinity, minY = Infinity, maxY = -Infinity
        for (const source of plottedSeries) for (const point of source.points || []) {
            if (!point) continue
            if (typeof point.x === "number" && isFinite(point.x)) {
                minX = Math.min(minX, point.x)
                maxX = Math.max(maxX, point.x)
            }
            if (root.validPoint(point)) {
                minY = Math.min(minY, point.y)
                maxY = Math.max(maxY, point.y)
            }
        }
        return {minX: isFinite(minX) ? minX : 0, maxX: isFinite(maxX) ? maxX : 1,
                minY: isFinite(minY) ? minY : 0, maxY: isFinite(maxY) ? maxY : 1}
    }
    property real xMinimum: dataRange.minX
    property real xMaximum: Math.max(xMinimum + 1, dataRange.maxX)
    property real yMinimum: Math.min(0, dataRange.minY)
    property real yMaximum: dataRange.maxY > yMinimum ? dataRange.maxY : yMinimum + 1
    readonly property bool hasPoints: plottedSeries.some(function(source) {
        return (source.points || []).some(function(point) { return root.validPoint(point) })
    })
    readonly property real plotHeight: plot.height
    property int yLabelCount: plot.height >= 48 ? 3 : plot.height >= 28 ? 2 : 1

    signal painted()
    function requestPaint() {
        if (active && visible && plot.available && plot.width > 0 && plot.height > 0) plot.requestPaint()
    }
    function validPoint(point) {
        return !!point && typeof point.x === "number" && isFinite(point.x)
            && typeof point.y === "number" && isFinite(point.y)
    }

    function option(source, name) { return source[name] !== undefined ? source[name] : root[name] }
    function pixelX(value) {
        return plotPadding + Math.max(0, plot.width - 2 * plotPadding)
            * (value - xMinimum) / (xMaximum > xMinimum ? xMaximum - xMinimum : 1)
    }
    function pixelY(value) {
        return plotPadding + Math.max(0, plot.height - 2 * plotPadding)
            * (1 - (value - yMinimum) / (yMaximum > yMinimum ? yMaximum - yMinimum : 1))
    }
    // Points are ordered by x. Missing samples stay in the search so gaps remain selectable.
    function insertionIndex(values, value) {
        let low = 0, high = values.length
        while (low < high) {
            const middle = Math.floor((low + high) / 2)
            if (values[middle].x < value) low = middle + 1
            else high = middle
        }
        return low
    }
    function pointAt(values, value, gap) {
        if (!values.length || value < values[0].x || value > values[values.length - 1].x) return null
        const index = insertionIndex(values, value)
        if (index < values.length && values[index].x === value)
            return validPoint(values[index]) ? values[index] : null
        if (index === 0 || index === values.length) return null
        const before = values[index - 1], after = values[index]
        if (!validPoint(before) || !validPoint(after) || after.x - before.x > (gap === undefined ? maximumGap : gap)) return null
        return value - before.x <= after.x - value ? before : after
    }
    function nearestX(value) {
        let nearest = NaN, distance = Infinity
        for (const values of selectionSeries) {
            const index = insertionIndex(values, value)
            for (let i = Math.max(0, index - 1); i < Math.min(values.length, index + 1); ++i) {
                const candidate = values[i].x
                if (isFinite(candidate) && candidate >= xMinimum && candidate <= xMaximum
                        && Math.abs(candidate - value) < distance) {
                    nearest = candidate
                    distance = Math.abs(candidate - value)
                }
            }
        }
        return nearest
    }
    function selectAt(position) {
        if (!interactive || !active || plot.width <= 2 * plotPadding) return
        const fraction = Math.max(0, Math.min(1, (position - plotPadding) / (plot.width - 2 * plotPadding)))
        interactionX = nearestX(xMinimum + (xMaximum - xMinimum) * fraction)
        scrubbed(interactionX)
    }
    function clearSelection() { interactionX = NaN; scrubbed(NaN) }
    function stepSelection(direction) {
        let next = direction > 0 ? Infinity : -Infinity
        const current = isFinite(selectedX) ? selectedX : direction > 0 ? xMinimum - 1 : xMaximum + 1
        for (const values of selectionSeries) {
            let index = insertionIndex(values, current)
            if (direction > 0) {
                if (index < values.length && values[index].x <= current) ++index
            } else --index
            if (index >= 0 && index < values.length) {
                const value = values[index].x
                if (value >= xMinimum && value <= xMaximum)
                    next = direction > 0 ? Math.min(next, value) : Math.max(next, value)
            }
        }
        if (isFinite(next)) { interactionX = next; scrubbed(next) }
    }
    activeFocusOnTab: interactive
    Keys.onLeftPressed: stepSelection(-1)
    Keys.onRightPressed: stepSelection(1)
    Keys.onEscapePressed: { clearSelection(); focus = false }
    onVisibleChanged: if (!visible) clearSelection()
    onInteractiveChanged: if (!interactive) clearSelection()
    onSeriesChanged: requestPaint()
    onLineDashPatternChanged: requestPaint()
    onPointsChanged: requestPaint()
    onActiveChanged: { if (active) requestPaint(); else clearSelection() }
    onSmoothChanged: requestPaint()
    onShowPointsChanged: requestPaint()
    onShowSegmentStartsChanged: requestPaint()
    onMaximumGapChanged: requestPaint()
    onLineWidthChanged: requestPaint()
    onPointRadiusChanged: requestPaint()
    onPlotPaddingChanged: requestPaint()
    onLineColorChanged: requestPaint()
    onGradientStopsChanged: requestPaint()
    onVerticalGradientChanged: requestPaint()
    onFillOpacityChanged: requestPaint()
    onShowGridChanged: requestPaint()
    onGridDivisionsChanged: requestPaint()
    onGridColorChanged: requestPaint()
    onGridDashPatternChanged: requestPaint()
    onReferenceLinesChanged: requestPaint()
    onXMinimumChanged: requestPaint()
    onXMaximumChanged: requestPaint()
    onYMinimumChanged: requestPaint()
    onYMaximumChanged: requestPaint()

    Canvas {
        id: plot
        objectName: root.plotObjectName
        x: root.leftInset
        width: Math.max(0, root.width - x)
        height: Math.max(0, root.height - root.bottomInset)
        antialiasing: true
        onAvailableChanged: if (available) root.requestPaint()
        onWidthChanged: root.requestPaint()
        onHeightChanged: root.requestPaint()
        onVisibleChanged: if (visible) root.requestPaint()
        onPainted: root.painted()
        onPaint: {
            if (!available || !root.active || !root.visible) return
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            const padding = root.plotPadding
            const w = width - 2 * padding, h = height - 2 * padding
            if (w <= 0 || h <= 0 || root.xMaximum <= root.xMinimum || root.yMaximum <= root.yMinimum) return
            const x = function(value) { return padding + w * (value - root.xMinimum) / (root.xMaximum - root.xMinimum) }
            const y = function(value) { return padding + h * (1 - (value - root.yMinimum) / (root.yMaximum - root.yMinimum)) }
            ctx.save()
            ctx.beginPath()
            ctx.rect(0, 0, width, height)
            ctx.clip()
            if (root.showGrid && root.gridDivisions > 0) {
                ctx.lineWidth = 1
                ctx.strokeStyle = root.gridColor
                ctx.setLineDash(root.gridDashPattern)
                for (let i = 0; i <= root.gridDivisions; ++i) {
                    const gridY = padding + h * i / root.gridDivisions
                    ctx.beginPath()
                    ctx.moveTo(padding, gridY)
                    ctx.lineTo(width - padding, gridY)
                    ctx.stroke()
                }
                ctx.setLineDash([])
            }
            for (const source of root.plottedSeries) {
                let stroke = root.option(source, "lineColor")
                const stops = root.option(source, "gradientStops")
                if (stops.length > 0) {
                    stroke = root.option(source, "verticalGradient")
                        ? ctx.createLinearGradient(0, padding, 0, height - padding)
                        : ctx.createLinearGradient(padding, 0, width - padding, 0)
                    for (const stop of stops) stroke.addColorStop(stop.position, stop.color)
                }
                ctx.strokeStyle = stroke
                ctx.lineWidth = root.option(source, "lineWidth")
                ctx.setLineDash(root.option(source, "lineDashPattern"))
                ctx.lineJoin = "round"
                ctx.lineCap = "round"

                const drawSegment = function(segment) {
                    if (!segment.length) return
                    ctx.beginPath()
                    ctx.moveTo(segment[0].x, segment[0].y)
                    for (let i = 1; i < segment.length; ++i) {
                        const previous = segment[i - 1], point = segment[i]
                        if (root.option(source, "smooth")) {
                            const middle = (previous.x + point.x) / 2
                            ctx.bezierCurveTo(middle, previous.y, middle, point.y, point.x, point.y)
                        } else ctx.lineTo(point.x, point.y)
                    }
                    if (segment.length > 1) ctx.stroke()
                    if (root.option(source, "fillOpacity") > 0 && segment.length > 1) {
                        ctx.lineTo(segment[segment.length - 1].x, height - padding)
                        ctx.lineTo(segment[0].x, height - padding)
                        ctx.closePath()
                        ctx.fillStyle = stroke
                        ctx.globalAlpha = Math.min(1, root.option(source, "fillOpacity"))
                        ctx.fill()
                        ctx.globalAlpha = 1
                    }
                    if (root.option(source, "showPoints") || root.option(source, "showSegmentStarts") || segment.length === 1) {
                        const markers = root.option(source, "showPoints") ? segment : [segment[0]]
                        for (const point of markers) {
                            ctx.fillStyle = point.color !== undefined ? point.color : stroke
                            ctx.beginPath()
                            ctx.arc(point.x, point.y, root.option(source, "pointRadius"), 0, 2 * Math.PI)
                            ctx.fill()
                        }
                    }
                }

                let segment = [], previousX = null
                for (const point of (source.points || [])) {
                    if (!root.validPoint(point)) {
                        drawSegment(segment)
                        segment = []
                        previousX = null
                        continue
                    }
                    if (previousX !== null && Math.abs(point.x - previousX) > root.option(source, "maximumGap")) {
                        drawSegment(segment)
                        segment = []
                    }
                    segment.push({x: x(point.x), y: y(point.y), color: point.color})
                    previousX = point.x
                }
                drawSegment(segment)
            }
            ctx.setLineDash([])
            for (const reference of root.referenceLines) {
                if (!isFinite(reference.value) || reference.value < root.yMinimum || reference.value > root.yMaximum) continue
                ctx.strokeStyle = reference.color
                ctx.lineWidth = reference.lineWidth !== undefined ? reference.lineWidth : 1
                ctx.setLineDash(reference.dashPattern || [])
                ctx.beginPath()
                ctx.moveTo(padding, y(reference.value))
                ctx.lineTo(width - padding, y(reference.value))
                ctx.stroke()
            }
            ctx.restore()
        }
    }

    Item {
        x: plot.x
        width: plot.width
        height: plot.height
        clip: true
        Rectangle {
            objectName: "lineChartCursor"
            visible: root.selectionVisible
            x: root.pixelX(root.selectedX) - width / 2
            y: root.plotPadding
            width: 1
            height: Math.max(0, parent.height - 2 * root.plotPadding)
            color: root.cursorColor
        }
        Repeater {
            model: root.selectedPoints
            delegate: Rectangle {
                required property var modelData
                required property int index
                visible: modelData !== null
                width: 9; height: 9; radius: width / 2
                x: modelData ? root.pixelX(modelData.x) - width / 2 : 0
                y: modelData ? root.pixelY(modelData.y) - height / 2 : 0
                color: modelData && modelData.color !== undefined ? modelData.color
                    : root.option(root.plottedSeries[index], "lineColor")
                border.width: 2
                border.color: Theme.color.neutral1
            }
        }
        MouseArea {
            id: pointer
            objectName: "lineChartPointer"
            anchors.fill: parent
            enabled: root.interactive && root.active
            hoverEnabled: true
            cursorShape: Qt.CrossCursor
            onPositionChanged: function(mouse) { root.selectAt(mouse.x) }
            onPressed: function(mouse) { root.forceActiveFocus(); root.selectAt(mouse.x) }
            onReleased: { root.focus = false; if (!containsMouse) root.clearSelection() }
            onExited: if (!pressed) root.clearSelection()
            onCanceled: { root.clearSelection(); root.focus = false }
        }
    }

    Repeater {
        model: root.showYAxis ? root.yLabelCount : 0
        delegate: CoreText {
            required property int index
            readonly property real fraction: index / Math.max(1, root.yLabelCount - 1)
            width: Math.max(0, root.leftInset - 6 * root.axisScale)
            y: root.plotPadding + (plot.height - root.plotPadding * 2) * fraction - height / 2
            text: root.yLabelFormatter(root.yMaximum - (root.yMaximum - root.yMinimum) * fraction)
            font: root.labelFont
            color: root.labelColor
            horizontalAlignment: Text.AlignRight
        }
    }
    Repeater {
        model: root.showXAxis ? root.xLabels : []
        delegate: CoreText {
            required property var modelData
            required property int index
            x: plot.x + (plot.width - width) * Math.max(0, Math.min(1,
                (modelData.value - root.xMinimum) / (root.xMaximum > root.xMinimum ? root.xMaximum - root.xMinimum : 1)))
            anchors.bottom: root.bottom
            text: modelData.text
            font: root.labelFont
            color: root.labelColor
        }
    }
}

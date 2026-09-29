// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtTest 1.2
import "../../qml/components/widgets"
import "../../qml/controls"

TestCase {
    id: testCase
    name: "FeeRatesWidget"
    when: windowShown
    width: 600
    height: 450
    visible: true

    QtObject {
        id: estimates
        property var rates: [12.345, 3.5, 0.5, 0.001]
        property real referenceRate: 2
        property bool ready: true
        property bool pending: false
        property bool active: false
    }
    Component {
        id: widgetComponent
        FeeRatesWidget {
            feeRatesModelRef: estimates
            Rectangle { anchors.fill: parent; z: -1; color: Theme.color.background }
        }
    }
    Component { id: registryComponent; DefaultWidgetRegistry {} }
    SignalSpy { id: paintSpy; signalName: "painted" }

    function init() {
        estimates.rates = [12.345, 3.5, 0.5, 0.001]
        estimates.referenceRate = 2
        estimates.ready = true
        estimates.pending = false
        estimates.active = false
        Theme.dark = true
    }

    function cleanup() { Theme.dark = true }

    function test_registeredSizes() {
        const registry = createTemporaryObject(registryComponent, testCase)
        compare(registry.catalog[1].id, "fee-rates")
        compare(registry.catalog[1].sizes.map(function(s) { return {columns: s.columns, rows: s.rows} }), [{columns: 2, rows: 1}, {columns: 3, rows: 2}, {columns: 1, rows: 1}])
        compare(registry.catalog[1].defaultSize, 0)
    }

    function test_sizesAndTheme_data() {
        return [
            {tag: "one-cell-large", width: 288, height: 288, columns: 1, rows: 1, dark: true},
            {tag: "one-cell", width: 112, height: 112, columns: 1, rows: 1, dark: true},
            {tag: "paper-small", width: 260, height: 112, columns: 2, rows: 1, dark: true},
            {tag: "paper-medium", width: 392, height: 244, columns: 3, rows: 2, dark: true},
            {tag: "compact-dark", columns: 2, rows: 1, width: 330, height: 148, dark: true},
            {tag: "compact-light", columns: 2, rows: 1, width: 330, height: 148, dark: false},
            {tag: "medium-constrained", width: 400, height: 220, columns: 3, rows: 2, dark: true},
            {tag: "large-dark", columns: 3, rows: 2, width: 520, height: 330, dark: true},
            {tag: "large-light", columns: 3, rows: 2, width: 520, height: 330, dark: false}
        ]
    }

    function test_sizesAndTheme(data) {
        Theme.dark = data.dark
        const widget = createTemporaryObject(widgetComponent, testCase, {
            columnSpan: data.columns, rowSpan: data.rows, width: data.width, height: data.height
        })
        verify(widget !== null)
        waitForRendering(widget)
        waitForPolish(widget)
        if (widget.compact) {
            const headline = findChild(widget, "feeRatesHeadline")
            const unit = findChild(widget, "feeRatesHeadlineUnit")
            const target = findChild(widget, "feeRatesHeadlineTarget")
            const separator = findChild(widget, "feeRatesCompactSeparator")
            const hour = findChild(widget, "feeRatesCompactHour")
            verify(!headline.truncated)
            compare(headline.text, Number(12.3).toLocaleString(Qt.locale(), 'f', 1))
            compare(hour.text, "0 sat/vB")
            fuzzyCompare(headline.mapToItem(widget, 0, headline.baselineOffset).y,
                         unit.mapToItem(widget, 0, unit.baselineOffset).y, 1)
            fuzzyCompare(unit.mapToItem(widget, 0, 0).x,
                         headline.mapToItem(widget, headline.width, 0).x + 6, 1)
            verify(target.mapToItem(widget, 0, 0).y - headline.mapToItem(widget, 0, headline.height).y <= 3)
            verify(separator.visible)
            verify(separator.y < hour.mapToItem(widget, 0, 0).y)
            return
        }
        compare(findChild(widget, "feeRatesHeadline").visible, false)
        const value = findChild(widget, "feeRateValue_2")
        compare(value.text, Number(0.5).toLocaleString(Qt.locale(), 'f', 1))
        compare(value.color, Theme.color.feeRateColors[0])
        compare(value.font.family, Theme.text.family)
        for (let i = 0; i < 4; ++i) {
            const target = findChild(widget, "feeRateTarget_" + i)
            compare(target.text, ["2 blocks", "4 blocks", "6 blocks", "100+ blocks"][i])
            compare(target.font, widget.footerLabelFont)
            const label = findChild(widget, "feeRateValue_" + i)
            verify(!label.truncated,
                   label.objectName + " must show the complete value (width=" + label.width
                   + ", contentWidth=" + label.contentWidth + ", height=" + label.height
                   + ", contentHeight=" + label.contentHeight + ", pixelSize=" + label.font.pixelSize + ")")
            const point = label.mapToItem(widget, 0, 0)
            verify(point.x >= 0 && point.y >= 0)
            verify(point.x + label.width <= widget.width + 1)
            verify(point.y + label.height <= widget.height + 1)
            verify(label.contentWidth <= label.width + 1,
                   label.objectName + " content exceeds width (width=" + label.width
                   + ", contentWidth=" + label.contentWidth + ", pixelSize=" + label.font.pixelSize + ")")
        }
        const curve = findChild(widget, "feeRatesCurve")
        verify(curve instanceof LineChart)
        if (curve.visible) {
            paintSpy.target = curve
            curve.requestPaint()
            paintSpy.wait()
        }
    }

    function test_unavailableAndPartialEstimates() {
        estimates.rates = [-1, -1, -1, -1]
        const widget = createTemporaryObject(widgetComponent, testCase, {width: 330, height: 148})
        compare(widget.hasEstimates, false)
        compare(findChild(widget, "feeRatesStatus").text, "Estimates unavailable")
        compare(findChild(widget, "feeRatesCurve").visible, false)
        compare(findChild(widget, "feeRateValue_0").text, "—")
        estimates.pending = true
        compare(findChild(widget, "feeRatesStatus").text, "Estimating fees…")
        estimates.ready = false
        compare(findChild(widget, "feeRatesStatus").text, "Waiting for node")
        estimates.rates = [2, -1, 0.1, NaN]
        compare(widget.hasEstimates, true)
        compare(widget.rateAt(1), -1)
        compare(widget.rateAt(3), -1)
    }

    function test_colorsFollowAmountsAndRecentHistory() {
        const widget = createTemporaryObject(widgetComponent, testCase, {width: 330, height: 148})
        estimates.rates = [1, 2, 4, 8]
        // With a recent median of 2, half/one/two/four times it hit the palette stops.
        for (let i = 0; i < 4; ++i) compare(widget.rateColors[i], Theme.color.feeRateColors[i])
        estimates.rates = [4, 4, 4, 4]
        for (let i = 0; i < 4; ++i) compare(widget.rateColors[i], Theme.color.orange)
        estimates.referenceRate = 8
        for (let i = 0; i < 4; ++i) compare(widget.rateColors[i], Theme.color.green)
        estimates.rates = [8, 1, 8, 1]
        compare(widget.rateColors[0], widget.rateColors[2])
        compare(widget.rateColors[1], widget.rateColors[3])
        estimates.referenceRate = -1
        for (let i = 0; i < 4; ++i) compare(widget.rateColors[i], Theme.color.neutral6)
        estimates.referenceRate = 2
        estimates.rates = [-1, 0, NaN, 0.001]
        for (let i = 0; i < 3; ++i) compare(widget.rateColors[i], Theme.color.neutral6)
        compare(widget.rateColors[3], Theme.color.green)
    }

    function test_activityRestoredAfterRemoval() {
        const widget = createTemporaryObject(widgetComponent, testCase, {width: 330, height: 148})
        compare(estimates.active, true)
        widget.active = false
        compare(estimates.active, false)
        widget.active = true
        compare(estimates.active, true)
        widget.destroy()
        wait(0)
        compare(estimates.active, false)
    }
}

// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15
import "../controls"

Rectangle {
    id: root
    property var series: []
    property var inspectionSeries: series
    property var xLabels: []
    property real xMinimum: 0
    property real xMaximum: 1
    property real yMaximum: 1
    property real maximumGap: Infinity
    property real selectedX: NaN
    property bool active: true
    property bool showReadout: false
    property var formatRate: function(value) { return {amount: String(value), unit: "B/s"} }
    readonly property bool overlay: series.length > 1
    readonly property alias lineChart: chart
    readonly property bool scrubbing: chart.scrubbing
    readonly property var inspectedPoints: inspectionSeries.map(function(source) {
        return chart.pointAt(source.points, root.selectedX, root.maximumGap)
    })
    signal scrubbed(real value)
    implicitHeight: overlay ? 464 : 224
    color: Theme.color.neutral1
    radius: 18

    component RateReading: RowLayout {
        property real rate: 0
        property color rateColor: Theme.color.neutral9
        readonly property var formatted: root.formatRate(rate, root.yMaximum)
        spacing: 8
        CoreText {
            text: parent.formatted.amount
            color: parent.rateColor
            font: Theme.text.chartValue.font
        }
        CoreText { text: parent.formatted.unit; font: Theme.text.description.font; color: Theme.color.neutral7 }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: root.width < 400 ? 16 : 24
        spacing: root.overlay ? 16 : 10
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: root.overlay ? 64 : 34
            spacing: root.width < 600 ? 24 : 48
            Repeater {
                model: root.series
                ColumnLayout {
                    required property var modelData
                    spacing: 4
                    RowLayout {
                        spacing: 10
                        Rectangle {
                            visible: root.overlay
                            width: 20; height: 3; radius: 1.5
                            color: modelData.lineColor
                        }
                        CoreText { text: modelData.name; font: root.overlay ? Theme.text.description.font : Theme.text.body.font }
                    }
                    RateReading { visible: root.overlay; rate: modelData.rate; rateColor: modelData.lineColor }
                }
            }
            Item { Layout.fillWidth: true }
            RateReading {
                visible: !root.overlay
                rate: root.series.length ? root.series[0].rate : 0
                rateColor: root.series.length ? root.series[0].lineColor : Theme.color.neutral9
            }
            CoreText {
                visible: root.overlay && root.width >= 680
                text: qsTr("Shared scale · %1").arg(root.formatRate(root.yMaximum, root.yMaximum).unit)
                font: Theme.text.description.font
                color: Theme.color.neutral7
            }
        }
        LineChart {
            id: chart
            objectName: root.objectName + "LineChart"
            Layout.fillWidth: true
            Layout.fillHeight: true
            active: root.active
            series: root.series
            xMinimum: root.xMinimum
            xMaximum: root.xMaximum
            yMinimum: 0
            yMaximum: root.yMaximum
            maximumGap: root.maximumGap
            lineWidth: 2
            interactive: true
            selectedX: root.selectedX
            onScrubbed: function(value) { root.scrubbed(value) }
            showGrid: true
            gridDivisions: root.overlay ? 4 : 2
            yLabelCount: root.overlay ? 5 : 3
            showYAxis: true
            showXAxis: true
            leftInset: 52
            bottomInset: 26
            xLabels: root.xLabels
            labelFont: Theme.text.monoCaption.font
            yLabelFormatter: function(value) {
                const divisor = Math.pow(1000, Math.max(0, Math.floor(Math.log(root.yMaximum) / Math.log(1000))))
                return Number(value / divisor).toLocaleString(Qt.locale(), 'f', value % divisor ? 1 : 0)
            }
        }
    }

    Rectangle {
        id: readout
        objectName: "networkTrafficReadout"
        visible: root.active && root.showReadout && chart.selectionVisible
        width: Math.min(260, root.width - 32)
        height: readoutLayout.implicitHeight + 32
        x: Math.max(16, Math.min(root.width - width - 16,
            chart.x + chart.selectedPixelX > width + 32
                ? chart.x + chart.selectedPixelX - width - 16 : chart.x + chart.selectedPixelX + 16))
        y: chart.y + 28
        color: Theme.color.neutral2
        border.color: Theme.color.neutral3
        radius: 12
        ColumnLayout {
            id: readoutLayout
            anchors.left: parent.left; anchors.right: parent.right
            anchors.top: parent.top; anchors.margins: 16
            spacing: 10
            CoreText { text: Qt.formatTime(new Date(root.selectedX), "hh:mm:ss"); font: Theme.text.monoCaption.font; color: Theme.color.neutral7 }
            Repeater {
                model: root.inspectionSeries
                RowLayout {
                    required property var modelData
                    required property int index
                    Layout.fillWidth: true
                    spacing: 8
                    Rectangle { width: 6; height: 6; radius: 3; color: modelData.lineColor }
                    CoreText { text: modelData.name; font: Theme.text.description.font }
                    Item { Layout.fillWidth: true }
                    CoreText {
                        readonly property var point: root.inspectedPoints[parent.index]
                        text: point ? root.formatRate(point.y).amount + " " + root.formatRate(point.y).unit : "—"
                        font: Theme.text.monoCaption.font
                        color: modelData.lineColor
                    }
                }
            }
        }
    }
}

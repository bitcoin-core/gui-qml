// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15
import "../../controls"

DashboardWidget {
    id: root
    objectName: "feeRatesWidget"
    property var feeRatesModelRef: typeof feeRatesModel !== "undefined" ? feeRatesModel : null
    readonly property var rates: feeRatesModelRef ? feeRatesModelRef.rates : []
    readonly property real referenceRate: feeRatesModelRef ? feeRatesModelRef.referenceRate : -1
    readonly property var rateColors: [colorForRate(rateAt(0)), colorForRate(rateAt(1)),
                                      colorForRate(rateAt(2)), colorForRate(rateAt(3))]
    readonly property bool hasEstimates: {
        for (let i = 0; i < 4; ++i) if (rateAt(i) > 0) return true
        return false
    }
    readonly property var targets: [qsTr("2 blocks"), qsTr("4 blocks"), qsTr("6 blocks"), qsTr("100+ blocks")]

    Binding {
        target: root.feeRatesModelRef
        property: "active"
        value: root.active
        when: root.feeRatesModelRef !== null
    }
    Component.onDestruction: if (feeRatesModelRef) feeRatesModelRef.active = false

    function rateAt(index) {
        const rate = Number(rates[index])
        return isFinite(rate) && rate > 0 ? rate : -1
    }

    function formattedRate(index) {
        const rate = rateAt(index)
        if (rate <= 0) return "—"
        if (compact) return rate.toLocaleString(Qt.locale(), 'f', 1)
        // Preserve sub-sat/vB precision from Core's sat/kvB estimates.
        const perKvb = Math.round(rate * 1000)
        const decimals = perKvb % 1000 === 0 ? 0 : perKvb % 100 === 0 ? 1 : perKvb % 10 === 0 ? 2 : 3
        return rate.toLocaleString(Qt.locale(), 'f', decimals)
    }

    function colorForRate(rate) {
        if (!(rate > 0) || !isFinite(rate) || !(referenceRate > 0)) return Theme.color.neutral6
        // Half the recent median is cool; each doubling moves one palette stop.
        const level = Math.max(0, Math.min(3, Math.log(rate / referenceRate) / Math.LN2 + 1))
        const lower = Math.floor(level)
        const blend = level - lower
        const palette = Theme.color.feeRateColors
        const a = palette[lower]
        const b = palette[Math.min(lower + 1, 3)]
        return Qt.rgba(a.r + (b.r - a.r) * blend, a.g + (b.g - a.g) * blend,
                       a.b + (b.b - a.b) * blend, 1)
    }

    FontMetrics { id: headlineMetrics; font: headline.font }

    ColumnLayout {
        anchors.fill: parent
        spacing: root.expanded ? 14 : 6
        RowLayout {
            Layout.fillWidth: true
            WidgetTitle {
                text: qsTr("Fee rates")
                Layout.fillWidth: true
            }
            CoreText { visible: !root.compact; text: qsTr("sat/vB"); font: root.primaryLabelFont; color: Theme.color.neutral6 }
        }
        ColumnLayout {
            visible: root.compact
            Layout.fillWidth: true
            spacing: 2
            Item {
                Layout.fillWidth: true
                implicitHeight: headline.implicitHeight
                CoreText {
                    id: headline
                    objectName: "feeRatesHeadline"
                    width: Math.max(0, Math.min(Math.ceil(headlineMetrics.advanceWidth(text)), parent.width - rateUnit.implicitWidth - 6))
                    text: root.formattedRate(0)
                    font: root.primaryValueFont
                    color: root.rateColors[0]
                    wrap: false
                    horizontalAlignment: Text.AlignLeft
                    fontSizeMode: Text.Fit
                    minimumPixelSize: root.scaledPixelSize(root.secondaryValueStyle)
                }
                CoreText {
                    id: rateUnit
                    objectName: "feeRatesHeadlineUnit"
                    anchors.left: headline.right
                    anchors.leftMargin: 6
                    anchors.baseline: headline.baseline
                    text: qsTr("sat/vB")
                    font: root.primaryLabelFont
                    color: Theme.color.neutral6
                    wrap: false
                }
            }
            CoreText { objectName: "feeRatesHeadlineTarget"; text: root.targets[0]; font: root.primaryLabelFont; color: Theme.color.neutral6 }
        }
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 0
            visible: !root.compact
            LineChart {
                id: curve
                objectName: "feeRatesCurve"
                anchors.fill: parent
                visible: root.hasEstimates
                active: root.active
                smooth: true
                showPoints: true
                lineWidth: 3
                pointRadius: 2.5
                plotPadding: 4
                xMinimum: 0
                xMaximum: 3
                yMinimum: 0
                points: [0, 1, 2, 3].map(function(index) {
                    return {x: index, y: root.rateAt(index) > 0 ? root.rateAt(index) : null,
                            color: root.rateColors[index]}
                })
                gradientStops: root.rateColors.map(function(color, index) {
                    return {position: index / 3, color: color}
                })
            }
        }
        CoreText {
            objectName: "feeRatesStatus"
            visible: !root.hasEstimates
            Layout.fillWidth: true
            font: root.footerLabelFont
            color: Theme.color.neutral7
            text: !root.feeRatesModelRef || !root.feeRatesModelRef.ready ? qsTr("Waiting for node")
                : root.feeRatesModelRef.pending ? qsTr("Estimating fees…") : qsTr("Estimates unavailable")
            elide: Text.ElideRight
            wrap: false
        }
        RowLayout {
            visible: !root.compact
            Layout.fillWidth: true
            spacing: root.expanded ? 4 : 0
            Repeater {
                model: 4
                ColumnLayout {
                    required property int index
                    implicitWidth: 0
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.preferredWidth: 1
                    spacing: 2
                    CoreText {
                        objectName: "feeRateTarget_" + parent.index
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        Layout.preferredWidth: 1
                        text: root.targets[parent.index]
                        font: root.footerLabelFont
                        color: Theme.color.neutral7
                        wrap: false
                        elide: Text.ElideRight
                    }
                    CoreText {
                        objectName: "feeRateValue_" + parent.index
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        Layout.preferredWidth: 1
                        text: root.formattedRate(parent.index)
                        font: root.secondaryValueFont
                        color: root.rateColors[parent.index]
                        fontSizeMode: Text.Fit
                        minimumPixelSize: root.footerValueStyle.pixelSize
                        wrap: false
                        Accessible.name: qsTr("%1: %2 sat/vB").arg(root.targets[parent.index]).arg(text)
                    }
                }
            }
        }
        Rectangle {
            objectName: "feeRatesCompactSeparator"
            visible: root.compact && root.hasEstimates
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.color.neutral3
        }
        RowLayout {
            visible: root.compact && root.hasEstimates
            Layout.fillWidth: true
            CoreText { Layout.fillWidth: true; text: root.targets[2]; font: root.footerLabelFont; color: Theme.color.neutral6; horizontalAlignment: Text.AlignLeft }
            CoreText { objectName: "feeRatesCompactHour"; text: root.rateAt(2) > 0 ? qsTr("%1 sat/vB").arg(Math.floor(root.rateAt(2)).toLocaleString(Qt.locale(), 'f', 0)) : "—"; font: root.footerValueFont; color: root.rateColors[2]; wrap: false }
        }
    }
}

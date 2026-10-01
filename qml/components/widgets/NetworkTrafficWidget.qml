// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15
import "../../controls"

DashboardWidget {
    id: root
    objectName: "networkTrafficWidget"
    property var trafficModel: typeof networkTrafficTower !== "undefined" ? networkTrafficTower : null
    readonly property var samples: renderingActive && trafficModel ? trafficModel.widgetHistory : []
    readonly property bool ready: samples.length > 0
    readonly property var latest: ready ? samples[samples.length - 1] : null
    readonly property var received: formatRate(latest ? latest.received : -1)
    readonly property var sent: formatRate(latest ? latest.sent : -1)
    readonly property int historyWindowMs: 5 * 60 * 1000
    readonly property real endTime: latest ? latest.time : 0
    readonly property real startTime: endTime - historyWindowMs
    readonly property var visibleSamples: samples.filter(function(sample) {
        return sample.time >= startTime && sample.time <= endTime
    })
    readonly property var series: [
        {points: visibleSamples.map(function(sample) { return {x: sample.time, y: sample.received} }), lineColor: Theme.color.blue},
        {points: visibleSamples.map(function(sample) { return {x: sample.time, y: sample.sent} }), lineColor: Theme.color.purple}
    ]
    readonly property real maximumRate: {
        let maximum = 1
        for (const sample of visibleSamples) maximum = Math.max(maximum, sample.received || 0, sample.sent || 0)
        return maximum * 1.1
    }
    activityTarget: trafficModel
    activityProperty: "widgetActive"
    Accessible.description: qsTr("Live received and sent data rates, recent traffic history, and totals since node startup.")

    function formatRate(value) {
        const units = [qsTr("B/s"), qsTr("KB/s"), qsTr("MB/s"), qsTr("GB/s"), qsTr("TB/s")]
        if (value < 0 || !isFinite(value)) return {amount: "—", unit: units[0]}
        let index = 0
        while (value >= 1000 && index < units.length - 1) { value /= 1000; ++index }
        return {amount: Number(value).toLocaleString(Qt.locale(), 'f', index === 0 ? 0 : 1), unit: units[index]}
    }
    function formatTotal(bytes) {
        const units = [qsTr("B"), qsTr("KB"), qsTr("MB"), qsTr("GB"), qsTr("TB"), qsTr("PB")]
        let index = 0, value = Math.max(0, bytes)
        while (value >= 1000 && index < units.length - 1) { value /= 1000; ++index }
        return Number(value).toLocaleString(Qt.locale(), 'f', index === 0 || Number.isInteger(value) ? 0 : value < 10 ? 2 : 1) + " " + units[index]
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: root.expanded ? 6 : 4
        WidgetTitle {
            objectName: "networkTrafficWidgetTitle"
            Layout.fillWidth: true
            text: qsTr("Network traffic")
        }
        RowLayout {
            visible: root.compact
            Layout.fillWidth: true
            spacing: 4
            Icon { source: "qrc:/icons/arrow-down.svg"; color: Theme.color.blue; size: 12 }
            CoreText {
                objectName: "networkTrafficCompactReceived"
                Layout.fillWidth: true
                text: root.received.amount + " " + root.received.unit
                font: root.primaryValueFont
                color: Theme.color.blue
                horizontalAlignment: Text.AlignLeft
                wrap: false
                fontSizeMode: Text.Fit
                minimumPixelSize: root.footerValueStyle.pixelSize
            }
        }
        RowLayout {
            visible: root.compact
            Layout.fillWidth: true
            spacing: 4
            Icon { source: "qrc:/icons/arrow-up.svg"; color: Theme.color.purple; size: 12 }
            CoreText {
                objectName: "networkTrafficCompactSent"
                Layout.fillWidth: true
                text: root.sent.amount + " " + root.sent.unit
                font: root.secondaryValueFont
                color: Theme.color.purple
                horizontalAlignment: Text.AlignLeft
                wrap: false
                fontSizeMode: Text.Fit
                minimumPixelSize: root.footerValueStyle.pixelSize
            }
        }
        RowLayout {
            visible: !root.compact
            Layout.fillWidth: true
            spacing: 16
            Repeater {
                model: [root.received, root.sent]
                ColumnLayout {
                    id: reading
                    required property int index
                    required property var modelData
                    readonly property color rateColor: index === 0 ? Theme.color.blue : Theme.color.purple
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.preferredWidth: 1
                    spacing: 2
                    RowLayout {
                        Layout.fillWidth: reading.index === 0
                        Layout.alignment: reading.index === 0 ? Qt.AlignLeft : Qt.AlignRight
                        Layout.maximumWidth: reading.width
                        spacing: 6
                        Icon {
                            visible: !root.expanded
                            source: reading.index === 0 ? "qrc:/icons/arrow-down.svg" : "qrc:/icons/arrow-up.svg"
                            color: reading.rateColor
                            size: Math.round(root.primaryValueFont.pixelSize * 0.75)
                        }
                        CoreText {
                            objectName: reading.index === 0 ? "networkTrafficReceivedRate" : "networkTrafficSentRate"
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            text: reading.modelData.amount + " " + reading.modelData.unit
                            font: root.primaryValueFont
                            color: reading.rateColor
                            horizontalAlignment: reading.index === 0 ? Text.AlignLeft : Text.AlignRight
                            wrap: false
                            fontSizeMode: Text.Fit
                            minimumPixelSize: root.scaledPixelSize(root.secondaryValueStyle)
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Item { visible: reading.index === 1; Layout.fillWidth: true }
                        Icon {
                            visible: root.expanded
                            source: reading.index === 0 ? "qrc:/icons/arrow-down.svg" : "qrc:/icons/arrow-up.svg"
                            color: Theme.color.neutral7
                            size: root.primaryLabelStyle.pixelSize
                        }
                        CoreText {
                            objectName: reading.index === 0 ? "networkTrafficReceivedLabel" : "networkTrafficSentLabel"
                            Layout.fillWidth: reading.index === 0
                            //: Label identifying incoming or outgoing traffic below its rate in the dashboard widget.
                            text: reading.index === 0 ? qsTr("Received") : qsTr("Sent")
                            font: root.expanded ? root.primaryLabelFont : root.footerLabelFont
                            color: Theme.color.neutral7
                            horizontalAlignment: reading.index === 0 ? Text.AlignLeft : Text.AlignRight
                            wrap: false
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 20
            LineChart {
                id: chart
                objectName: "networkTrafficWidgetChart"
                anchors.fill: parent
                active: root.renderingActive
                series: root.series
                xMinimum: root.startTime
                xMaximum: root.endTime
                yMinimum: 0
                yMaximum: root.maximumRate
                maximumGap: Math.max(3000, root.historyWindowMs / 480 * 3)
                lineWidth: 2
                showSegmentStarts: true
                showGrid: root.expanded
                gridDivisions: 2
                showXAxis: root.expanded && root.ready
                labelFont: root.footerLabelFont
                xLabels: root.expanded && root.ready ? [
                    {value: root.startTime, text: Qt.formatTime(new Date(root.startTime), "hh:mm")},
                    {value: (root.startTime + root.endTime) / 2, text: Qt.formatTime(new Date((root.startTime + root.endTime) / 2), "hh:mm")},
                    {value: root.endTime, text: Qt.formatTime(new Date(root.endTime), "hh:mm")}
                ] : []
                visible: root.ready
            }
            CoreText {
                objectName: "networkTrafficWidgetStatus"
                anchors.centerIn: parent
                width: parent.width
                visible: !root.ready
                text: root.trafficModel ? qsTr("Collecting traffic…") : qsTr("Waiting for node")
                font: root.footerLabelFont
                color: Theme.color.neutral7
                maximumLineCount: 2
                elide: Text.ElideRight
            }
        }
        WidgetSection {
            objectName: "networkTrafficWidgetTotals"
            visible: root.expanded
            Layout.fillWidth: true
            padding: 12
            background: Rectangle { radius: 10; color: Theme.color.neutral1 }
            contentItem: RowLayout {
                spacing: 16
                Repeater {
                    model: [qsTr("Session received"), qsTr("Session sent")]
                    ColumnLayout {
                        required property var modelData
                        required property int index
                        Layout.fillWidth: true
                        spacing: 4
                        CoreText { text: modelData; font: root.footerLabelFont; color: Theme.color.neutral7; wrap: false }
                        CoreText {
                            objectName: index === 0 ? "networkTrafficWidgetTotalReceived" : "networkTrafficWidgetTotalSent"
                            text: root.trafficModel ? root.formatTotal(index === 0 ? root.trafficModel.totalBytesReceived : root.trafficModel.totalBytesSent) : "—"
                            font: root.footerValueFont
                            wrap: false
                        }
                    }
                }
            }
        }
    }
}

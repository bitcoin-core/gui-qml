// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15
import "../../controls"
import "../../components"

ColumnLayout {
    id: root
    objectName: "networkTrafficPage"
    property var trafficModel: typeof networkTrafficTower !== "undefined" ? networkTrafficTower : null
    property bool active: visible
    property int trafficGraphScale: 300
    property bool overlay: false
    property real selectedTime: NaN
    property string inspectedGraph: ""
    property var frozenHistory: null
    readonly property var liveHistory: trafficModel ? trafficModel.history : []
    readonly property var history: frozenHistory !== null ? frozenHistory : liveHistory
    readonly property real endTime: history.length ? history[history.length - 1].time : 0
    readonly property real startTime: endTime - trafficGraphScale * 1000
    readonly property var scaleOptions: [
        {text: qsTr("5 min"), seconds: 300}, {text: qsTr("1 hour"), seconds: 3600},
        {text: qsTr("12 hours"), seconds: 43200}, {text: qsTr("1 day"), seconds: 86400}
    ]
    readonly property var visibleHistory: history.filter(function(sample) { return sample.time >= startTime && sample.time <= endTime })
    readonly property var trafficSeries: [
        {name: qsTr("Received"), lineColor: Theme.color.blue, fillOpacity: overlay ? 0.04 : 0.08,
         rate: liveHistory.length ? liveHistory[liveHistory.length - 1].received : 0,
         points: visibleHistory.map(function(sample) { return {x: sample.time, y: sample.received} })},
        {name: qsTr("Sent"), lineColor: Theme.color.purple,
         fillOpacity: overlay ? 0 : 0.08,
         rate: liveHistory.length ? liveHistory[liveHistory.length - 1].sent : 0,
         points: visibleHistory.map(function(sample) { return {x: sample.time, y: sample.sent} })}
    ]
    readonly property var timeLabels: {
        const labels = []
        const count = width >= 640 ? 6 : 3
        for (let i = 0; i < count; ++i) {
            const time = startTime + (endTime - startTime) * i / (count - 1)
            labels.push({value: time, text: endTime ? Qt.formatTime(new Date(time), "hh:mm") : ""})
        }
        return labels
    }
    readonly property bool scrubbing: receivedGraph.scrubbing || sentGraph.scrubbing || overlayGraph.scrubbing
    spacing: 28

    function scaleIndex(scale) {
        for (let i = 0; i < scaleOptions.length; ++i) if (scaleOptions[i].seconds === scale) return i
        return 0
    }
    function selectScale(scale) { trafficGraphScale = scale }
    function clearInspection() { selectedTime = NaN; inspectedGraph = ""; frozenHistory = null }
    function inspect(name, value) { selectedTime = value; inspectedGraph = isFinite(value) ? name : "" }
    function formatBytes(bytes) {
        const units = [qsTr("B"), qsTr("KB"), qsTr("MB"), qsTr("GB"), qsTr("TB"), qsTr("PB")]
        let index = 0, value = Math.max(0, bytes)
        while (value >= 1000 && index < units.length - 1) { value /= 1000; ++index }
        return {amount: Number(value).toLocaleString(Qt.locale(), 'f', index === 0 || Number.isInteger(value) ? 0 : value < 10 ? 2 : 1), unit: units[index]}
    }
    function formatRate(value, maximum) {
        const units = [qsTr("B/s"), qsTr("KB/s"), qsTr("MB/s"), qsTr("GB/s"), qsTr("TB/s")]
        const scale = maximum === undefined ? Math.max(rateMaximum(0), rateMaximum(1)) : maximum
        const index = Math.min(units.length - 1, Math.max(0, Math.floor(Math.log(Math.max(1, scale)) / Math.log(1000))))
        const amount = value / Math.pow(1000, index)
        return {amount: Number(amount).toLocaleString(Qt.locale(), 'f', Number.isInteger(amount) ? 0 : 1), unit: units[index]}
    }
    function rateMaximum(index) {
        let maximum = 1
        for (const point of trafficSeries[index].points) if (point.y !== null) maximum = Math.max(maximum, point.y)
        const magnitude = Math.pow(10, Math.floor(Math.log(maximum) / Math.LN10))
        return Math.ceil(maximum / magnitude) * magnitude
    }
    function updateActivity() { if (trafficModel) trafficModel.active = active }
    onActiveChanged: { updateActivity(); if (!active) clearInspection() }
    onTrafficGraphScaleChanged: {
        clearInspection()
        if (trafficModel) trafficModel.updateFilterWindowSize(trafficGraphScale / 10)
    }
    onOverlayChanged: clearInspection()
    onScrubbingChanged: frozenHistory = scrubbing ? liveHistory.slice() : null
    Component.onCompleted: {
        if (trafficModel) trafficModel.updateFilterWindowSize(trafficGraphScale / 10)
        updateActivity()
    }
    Component.onDestruction: if (trafficModel) trafficModel.active = false

    AppSettings {
        property alias trafficGraphScale: root.trafficGraphScale
        property alias trafficGraphOverlay: root.overlay
    }

    RowLayout {
        Layout.fillWidth: true
        CoreText {
            objectName: "networkTrafficHeadingDescription"
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignLeft
            text: qsTr("Data exchanged by your node.")
            font: Theme.text.description.font
            color: Theme.color.neutral7
        }
        StatusPill {
            objectName: "networkTrafficLivePill"
            text: root.active ? qsTr("Live") : qsTr("Paused")
            accentColor: root.active ? Theme.color.green : Theme.color.neutral7
        }
    }
    GridLayout {
        Layout.fillWidth: true
        columns: root.width >= 760 ? 2 : 1
        rowSpacing: 12
        columnSpacing: 24
        SegmentedPicker {
            objectName: "networkTrafficRangePicker"
            Layout.fillWidth: true
            Layout.maximumWidth: root.width >= 760 ? 484 : root.width
            model: root.scaleOptions
            currentIndex: root.scaleIndex(root.trafficGraphScale)
            onSelected: function(index, option) { root.selectScale(option.seconds) }
        }
        Item {
            Layout.fillWidth: true
            implicitWidth: 236
            implicitHeight: modePicker.implicitHeight
            SegmentedPicker {
                id: modePicker
                objectName: "networkTrafficModePicker"
                anchors.right: parent.right
                width: Math.min(236, parent.width)
                height: parent.height
                activeBackgroundColor: Theme.color.neutral3
                activeTextColor: Theme.color.neutral9
                model: [qsTr("Separate"), qsTr("Overlay")]
                currentIndex: root.overlay ? 1 : 0
                onSelected: function(index) { root.overlay = index === 1 }
            }
        }
    }
    ColumnLayout {
        objectName: "networkTrafficSection"
        Layout.fillWidth: true
        spacing: 16
        NetworkTrafficGraph {
            id: receivedGraph
            objectName: "networkTrafficReceivedGraph"
            Layout.fillWidth: true
            Layout.preferredHeight: implicitHeight
            visible: !root.overlay
            active: root.active && visible
            series: [root.trafficSeries[0]]
            inspectionSeries: root.trafficSeries
            xMinimum: root.startTime; xMaximum: root.endTime
            yMaximum: root.rateMaximum(0)
            maximumGap: Math.max(3000, root.trafficGraphScale * 1000 / 480 * 3)
            xLabels: root.timeLabels
            formatRate: root.formatRate
            selectedX: root.selectedTime
            showReadout: root.inspectedGraph === "received"
            onScrubbed: function(value) { root.inspect("received", value) }
        }
        NetworkTrafficGraph {
            id: sentGraph
            objectName: "networkTrafficSentGraph"
            Layout.fillWidth: true
            Layout.preferredHeight: implicitHeight
            visible: !root.overlay
            active: root.active && visible
            series: [root.trafficSeries[1]]
            inspectionSeries: root.trafficSeries
            xMinimum: root.startTime; xMaximum: root.endTime
            yMaximum: root.rateMaximum(1)
            maximumGap: receivedGraph.maximumGap
            xLabels: root.timeLabels
            formatRate: root.formatRate
            selectedX: root.selectedTime
            showReadout: root.inspectedGraph === "sent"
            onScrubbed: function(value) { root.inspect("sent", value) }
        }
        NetworkTrafficGraph {
            id: overlayGraph
            objectName: "networkTrafficOverlayGraph"
            Layout.fillWidth: true
            Layout.preferredHeight: implicitHeight
            visible: root.overlay
            active: root.active && visible
            series: root.trafficSeries
            xMinimum: root.startTime; xMaximum: root.endTime
            yMaximum: Math.max(root.rateMaximum(0), root.rateMaximum(1))
            maximumGap: receivedGraph.maximumGap
            xLabels: root.timeLabels
            formatRate: root.formatRate
            selectedX: root.selectedTime
            showReadout: root.inspectedGraph === "overlay"
            onScrubbed: function(value) { root.inspect("overlay", value) }
        }
        CoreText {
            objectName: "networkTrafficEmptyStatus"
            visible: !root.history.length
            Layout.fillWidth: true
            text: qsTr("Collecting network traffic…")
            font: Theme.text.description.font
            color: Theme.color.neutral7
        }
    }
    ColumnLayout {
        objectName: "networkTrafficTotalsSection"
        Layout.fillWidth: true
        spacing: 14
        RowLayout {
            Layout.fillWidth: true
            CoreText { Layout.fillWidth: true; horizontalAlignment: Text.AlignLeft; text: qsTr("Totals"); font: Theme.text.heading.font }
            CoreText { text: qsTr("Since startup"); font: Theme.text.description.font; color: Theme.color.neutral7 }
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: root.width < 600 ? 160 : 96
            color: Theme.color.neutral1
            radius: 18
            GridLayout {
                anchors.fill: parent; anchors.margins: 24
                columns: root.width < 600 ? 1 : 2
                columnSpacing: 64; rowSpacing: 20
                Repeater {
                    model: root.trafficSeries
                    RowLayout {
                        required property var modelData
                        required property int index
                        readonly property var formatted: root.formatBytes(root.trafficModel
                            ? index === 0 ? root.trafficModel.totalBytesReceived : root.trafficModel.totalBytesSent : 0)
                        Layout.fillWidth: true
                        spacing: 10
                        Rectangle { width: 7; height: 7; radius: 3.5; color: modelData.lineColor }
                        CoreText { text: modelData.name; font: Theme.text.description.font; color: Theme.color.neutral8 }
                        Item { Layout.fillWidth: true }
                        CoreText {
                            objectName: index === 0 ? "networkTrafficTotalReceived" : "networkTrafficTotalSent"
                            text: parent.formatted.amount + " " + parent.formatted.unit
                            font: Theme.text.chartValue.font
                        }
                    }
                }
            }
        }
    }
}

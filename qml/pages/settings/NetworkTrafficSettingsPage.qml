pragma ComponentBehavior: Bound

// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import org.bitcoincore.qt 1.0

import "../../controls"
import "../../components"

SettingsPage {
    id: root
    objectName: "networkTrafficSettingsPage"
    title: qsTr("Network traffic")
    showBackButton: false
    maximumContentWidth: width

    property int trafficGraphScale: 300
    property bool ownsNetworkTrafficActivity: false
    readonly property var scaleOptions: [
        { text: qsTr("5 min"), seconds: 300 },
        { text: qsTr("1 hour"), seconds: 3600 },
        { text: qsTr("12 hours"), seconds: 3600 * 12 },
        { text: qsTr("1 day"), seconds: 3600 * 24 }
    ]

    function scaleIndex(scale) {
        for (let index = 0; index < root.scaleOptions.length; ++index) {
            if (root.scaleOptions[index].seconds === scale) return index
        }
        return 0
    }

    function selectScale(scale) {
        root.trafficGraphScale = scale
        networkTrafficTower.updateFilterWindowSize(scale / 10)
    }

    function formatBytes(bytes) {
        const suffixes = ["Bytes", "KB", "MB", "GB", "TB", "PB"]
        let index = 0
        while (bytes >= 1000 && index < suffixes.length - 1) {
            bytes /= 1000
            index++
        }
        return bytes.toFixed(0) + " " + suffixes[index]
    }

    function updateNetworkTrafficActivity() {
        if (root.visible) {
            networkTrafficTower.active = true
            root.ownsNetworkTrafficActivity = true
        } else {
            if (root.ownsNetworkTrafficActivity) networkTrafficTower.active = false
            root.ownsNetworkTrafficActivity = false
        }
    }

    AppSettings {
        id: settings
        property alias trafficGraphScale: root.trafficGraphScale
    }

    PageHeading {
        objectName: "networkTrafficHeading"
        Layout.fillWidth: true
        description: qsTr("How much data you have sent to and received from your peers.")
    }

    FormSection {
        objectName: "networkTrafficSection"
        Layout.fillWidth: true

        SegmentedPicker {
            objectName: "networkTrafficRangePicker"
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.topMargin: 16
            Layout.bottomMargin: 6
            implicitHeight: 36
            model: root.scaleOptions
            currentIndex: root.scaleIndex(root.trafficGraphScale)
            onSelected: function(index, option) {
                root.selectScale(option.seconds)
            }
        }

        ValueRow {
            objectName: "networkTrafficReceivedRow"
            Layout.fillWidth: true
            title: qsTr("Received")
            value: root.formatBytes(networkTrafficTower.totalBytesReceived)
            showDivider: false
            leadingItem: Rectangle {
                implicitWidth: 10
                implicitHeight: 10
                radius: width / 2
                color: Theme.color.green
            }
            bodyItem: NetworkTrafficGraph {
                objectName: "networkTrafficReceivedGraph"
                Layout.fillWidth: true
                Layout.preferredHeight: 250
                backgroundColor: Theme.color.neutral1
                borderColor: Theme.color.neutral3
                fillColor: Theme.color.green
                lineColor: Theme.color.green
                markerLineColor: Theme.color.neutral3
                unitLabelColor: Theme.color.neutral7
                maxSamples: root.trafficGraphScale
                maxValue: networkTrafficTower.maxReceivedRateBps
                valueList: networkTrafficTower.receivedRateList
                maxRateBps: networkTrafficTower.maxReceivedRateBps
            }
        }

        ValueRow {
            objectName: "networkTrafficSentRow"
            Layout.fillWidth: true
            title: qsTr("Sent")
            value: root.formatBytes(networkTrafficTower.totalBytesSent)
            showDivider: false
            bottomPadding: 16
            leadingItem: Rectangle {
                implicitWidth: 10
                implicitHeight: 10
                radius: width / 2
                color: Theme.color.blue
            }
            bodyItem: NetworkTrafficGraph {
                objectName: "networkTrafficSentGraph"
                Layout.fillWidth: true
                Layout.preferredHeight: 250
                backgroundColor: Theme.color.neutral1
                borderColor: Theme.color.neutral3
                fillColor: Theme.color.blue
                lineColor: Theme.color.blue
                markerLineColor: Theme.color.neutral3
                unitLabelColor: Theme.color.neutral7
                maxSamples: root.trafficGraphScale
                maxValue: networkTrafficTower.maxSentRateBps
                valueList: networkTrafficTower.sentRateList
                maxRateBps: networkTrafficTower.maxSentRateBps
            }
        }
    }

    Component.onCompleted: {
        networkTrafficTower.updateFilterWindowSize(root.trafficGraphScale / 10)
        root.updateNetworkTrafficActivity()
    }
    onVisibleChanged: root.updateNetworkTrafficActivity()
    Component.onDestruction: {
        if (root.ownsNetworkTrafficActivity) networkTrafficTower.active = false
    }
}

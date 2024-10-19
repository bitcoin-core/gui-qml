// Copyright (c) 2023 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import Qt.labs.settings 1.0
import "../../controls"
import "../../components"

import org.bitcoincore.qt 1.0

InformationPage {
    id: root
    property int trafficGraphScale: 300

    Settings {
        id: settings
        property alias trafficGraphScale: root.trafficGraphScale
    }
    navLeftDetail: NavButton {
        iconSource: "image://images/caret-left"
        text: qsTr("Back")
        onClicked: root.back()
    }
    navMiddleDetail: Header {
        headerBold: true
        headerSize: 18
        header: qsTr("Network traffic")
    }
    bannerActive: false
    bold: true
    headerText: qsTr("Network Traffic")
    headerMargin: 0
    description: qsTr("How much data you have sent to and received from your peers.")
    descriptionMargin: 15
    detailActive: true
    detailMaximumWidth: 600
    detailItem: ColumnLayout {
        width: parent.width
        spacing: 15

        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            color: Theme.color.neutral3
            width: scaleRow.implicitWidth + 6
            height: scaleRow.implicitHeight + 6
            radius: 3

            Behavior on color {
                ColorAnimation { duration: 150 }
            }

            RowLayout {
                id: scaleRow
                anchors.centerIn: parent
                anchors.margins: 3
                spacing: 5

                ToggleButton {
                    text: qsTr("5 min")
                    autoExclusive: true
                    checked: root.trafficGraphScale === 300
                    bgRadius: 3
                    textColor: Theme.color.neutral9
                    textActiveColor: Theme.color.neutral0
                    bgActiveColor: Theme.color.neutral9
                    bgDefaultColor: Theme.color.neutral3

                    onClicked: {
                        root.trafficGraphScale = 300
                        networkTrafficTower.updateFilterWindowSize(root.trafficGraphScale / 10)
                    }
                }

                ToggleButton {
                    text: qsTr("1 hour")
                    autoExclusive: true
                    checked: root.trafficGraphScale === 3600
                    bgRadius: 3
                    textColor: Theme.color.neutral9
                    textActiveColor: Theme.color.neutral0
                    bgActiveColor: Theme.color.neutral9
                    bgDefaultColor: Theme.color.neutral3

                    onClicked: {
                        root.trafficGraphScale = 3600
                        networkTrafficTower.updateFilterWindowSize(root.trafficGraphScale / 10)
                    }
                }

                ToggleButton {
                    text: qsTr("12 hours")
                    autoExclusive: true
                    checked: root.trafficGraphScale === 3600 * 12
                    bgRadius: 3
                    textColor: Theme.color.neutral9
                    textActiveColor: Theme.color.neutral0
                    bgActiveColor: Theme.color.neutral9
                    bgDefaultColor: Theme.color.neutral3

                    onClicked: {
                        root.trafficGraphScale = 3600 * 12
                        networkTrafficTower.updateFilterWindowSize(root.trafficGraphScale / 10)
                    }
                }

                ToggleButton {
                    text: qsTr("1 day")
                    autoExclusive: true
                    checked: root.trafficGraphScale === 3600 * 24
                    bgRadius: 3
                    textColor: Theme.color.neutral9
                    textActiveColor: Theme.color.neutral0
                    bgActiveColor: Theme.color.neutral9
                    bgDefaultColor: Theme.color.neutral3

                    onClicked: {
                        root.trafficGraphScale = 3600 * 24
                        networkTrafficTower.updateFilterWindowSize(root.trafficGraphScale / 10)
                    }
                }
            }
        }

        TotalBytesIndicator {
            Layout.alignment: Qt.AlignHCenter
            indicatorText: qsTr("Received: %1").arg(formatBytes(networkTrafficTower.totalBytesReceived))
            indicatorColor: Theme.color.green
        }

        NetworkTrafficGraph {
            id: receivedGraph
            Layout.fillWidth: true
            backgroundColor: Theme.color.background
            borderColor: Theme.color.neutral5
            fillColor: Theme.color.green
            lineColor: Theme.color.green
            markerLineColor: Theme.color.neutral2
            maxSamples: root.trafficGraphScale
            maxValue: networkTrafficTower.maxReceivedRateBps
            valueList: networkTrafficTower.receivedRateList
            maxRateBps: networkTrafficTower.maxReceivedRateBps
        }

        TotalBytesIndicator {
            Layout.alignment: Qt.AlignHCenter
            indicatorText: qsTr("Sent: %1").arg(formatBytes(networkTrafficTower.totalBytesSent))
            indicatorColor: Theme.color.blue
        }

        NetworkTrafficGraph {
            Layout.fillWidth: true
            backgroundColor: Theme.color.background
            borderColor: Theme.color.neutral5
            fillColor: Theme.color.blue
            lineColor: Theme.color.blue
            markerLineColor: Theme.color.neutral2
            maxSamples: root.trafficGraphScale
            maxValue: networkTrafficTower.maxSentRateBps
            valueList: networkTrafficTower.sentRateList
            maxRateBps: networkTrafficTower.maxSentRateBps
        }
    }

    function formatBytes(bytes) {
        var suffixes = ["Bytes", "KB", "MB", "GB", "TB", "PB"];
        var index = 0;
        while (bytes >= 1000 && index < suffixes.length - 1) {
            bytes /= 1000;
            index++;
        }
        return bytes.toFixed(0) + " " + suffixes[index];
    }
}

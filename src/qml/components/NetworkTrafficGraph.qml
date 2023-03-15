// Copyright (c) 2023 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../controls"
import "../components"

import org.bitcoincore.qt 1.0

Item {
    id: root
    property alias backgroundColor: trafficGraph.backgroundColor
    property alias borderColor: trafficGraph.borderColor
    property alias fillColor: trafficGraph.fillColor
    property alias lineColor: trafficGraph.lineColor
    property alias markerLineColor: trafficGraph.markerLineColor
    property alias maxSamples: trafficGraph.maxSamples
    property alias maxValue: trafficGraph.maxValue
    property alias valueList: trafficGraph.valueList
    property double maxRateBps
    property color unitLabelColor: Theme.color.neutral9

    height: 250

    LineGraph {
        id: trafficGraph
        height: root.height
        width: root.width
        backgroundColor: root.backgroundColor
        borderColor: root.borderColor
        fillColor: root.fillColor
        lineColor: root.lineColor
        markerLineColor: root.markerLineColor
        maxSamples: root.maxSamples
        maxValue: root.maxValue
        valueList: root.valueList

        Behavior on backgroundColor {
            ColorAnimation { duration: 150 }
        }

        Behavior on borderColor {
            ColorAnimation { duration: 150 }
        }

        Behavior on fillColor {
            ColorAnimation { duration: 150 }
        }

        Behavior on lineColor {
            ColorAnimation { duration: 150 }
        }

        Behavior on markerLineColor {
            ColorAnimation { duration: 150 }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        CoreText {
            text: formatBytes(formatMagnitude(root.maxRateBps)) + "/s"
            color: root.unitLabelColor
            font.pixelSize: 13
            Layout.alignment: Qt.AlignLeft | Qt.AlignBottom
            Layout.leftMargin: 5
            Layout.bottomMargin: 6
        }
        CoreText {
            text: formatBytes(formatMagnitude(root.maxRateBps / 10)) + "/s"
            color: root.unitLabelColor
            font.pixelSize: 13
            Layout.alignment: Qt.AlignLeft | Qt.AlignBottom
            Layout.leftMargin: 5
            Layout.bottomMargin: 5
        }
    }

    function formatMagnitude(max_rate) {
        var base = Math.floor(Math.log10(max_rate));
        return Math.pow(10.0, base);
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

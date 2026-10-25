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
    property alias fillColor: trafficGraph.gradientColor
    property alias lineColor: trafficGraph.lineColor
    property alias markerLineColor: trafficGraph.markerLineColor
    property alias maxSamples: trafficGraph.maxSamples
    property alias maxValue: trafficGraph.maxValue
    property alias valueList: trafficGraph.valueList
    property double maxRateBps
    property color unitLabelColor: Theme.color.neutral7

    height: 250

    LineGraph {
        id: trafficGraph
        height: root.height
        width: root.width
        backgroundColor: Theme.color.surface1
        borderColor: Theme.color.surfaceDivider
        gradientColor: Theme.color.trafficReceived
        lineColor: Theme.color.trafficReceived
        markerLineColor: Theme.color.surfaceDivider
        maxSamples: root.maxSamples
        maxValue: root.maxValue
        valueList: root.valueList

        Behavior on backgroundColor {
            enabled: Theme.motion.animated
            ColorAnimation { duration: Theme.motion.normal }
        }

        Behavior on borderColor {
            enabled: Theme.motion.animated
            ColorAnimation { duration: Theme.motion.normal }
        }

        Behavior on gradientColor {
            enabled: Theme.motion.animated
            ColorAnimation { duration: Theme.motion.normal }
        }

        Behavior on lineColor {
            enabled: Theme.motion.animated
            ColorAnimation { duration: Theme.motion.normal }
        }

        Behavior on markerLineColor {
            enabled: Theme.motion.animated
            ColorAnimation { duration: Theme.motion.normal }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        CoreText {
            //: Network traffic rate axis label. %1 is a size such as "10 KB".
            text: qsTr("%1/s").arg(formatBytes(formatMagnitude(root.maxRateBps)))
            color: root.unitLabelColor
            textStyle: Theme.text.monoCaption
            Layout.alignment: Qt.AlignLeft | Qt.AlignBottom
            Layout.leftMargin: 5
            Layout.bottomMargin: Theme.space.s
        }
        CoreText {
            text: qsTr("%1/s").arg(formatBytes(formatMagnitude(root.maxRateBps / 10)))
            color: root.unitLabelColor
            textStyle: Theme.text.monoCaption
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
        var units = [qsTr("%1 Bytes"), qsTr("%1 KB"), qsTr("%1 MB"), qsTr("%1 GB"), qsTr("%1 TB"), qsTr("%1 PB")];
        var index = 0;
        while (bytes >= 1000 && index < units.length - 1) {
            bytes /= 1000;
            index++;
        }
        return units[index].arg(Number(bytes).toLocaleString(Qt.locale(), "f", 0));
    }
}

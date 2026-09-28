// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15
import "../../controls"

DashboardWidget {
    id: root
    objectName: "mempoolWidget"
    property var activityModel: typeof mempoolActivityModel !== "undefined" ? mempoolActivityModel : null
    property var nodeModelRef: typeof nodeModel !== "undefined" ? nodeModel : null
    readonly property bool available: nodeModelRef !== null && nodeModelRef.mempoolInformationAvailable
    readonly property bool ready: available && activityModel !== null && activityModel.ready
    readonly property real capacityBaseline: activityModel ? activityModel.baseline : 1000000 / 600
    readonly property real currentRate: ready ? activityModel.incomingRate : -1
    readonly property var samples: ready ? activityModel.history : []
    Accessible.description: qsTr("Incoming virtual bytes per second. The dashed line is approximately one block of capacity per ten minutes.")

    Binding { target: root.activityModel; property: "active"; value: root.active && root.available; when: root.activityModel !== null }
    Component.onDestruction: if (activityModel) activityModel.active = false

    function number(value, decimals) { return Number(value).toLocaleString(Qt.locale(), 'f', decimals) }
    ColumnLayout {
        anchors.fill: parent
        spacing: root.expanded ? 12 : 6
        RowLayout {
            Layout.fillWidth: true
            WidgetTitle {
                objectName: "incomingTransactionsTitle"
                text: root.width < 300 ? qsTr("Incoming tx") : qsTr("Incoming transactions")
                Layout.fillWidth: true
            }
            CoreText {
                objectName: "incomingTransactionsRate"
                visible: !root.compact
                text: root.currentRate >= 0 ? qsTr("%1 vB/s").arg(root.number(root.currentRate, 0)) : qsTr("— vB/s")
                font: root.scaledFont(Theme.text.captionStrong)
                color: root.currentRate >= 0 ? chart.colorForRate(root.currentRate) : Theme.color.neutral6
                wrap: false
            }
        }
        CoreText {
            visible: root.compact
            Layout.fillWidth: true
            text: root.currentRate >= 0 ? qsTr("%1 vB/s").arg(root.number(root.currentRate, 0)) : qsTr("— vB/s")
            font: root.scaledFont(Theme.text.heading)
            horizontalAlignment: Text.AlignLeft
            wrap: false
            fontSizeMode: Text.Fit
            minimumPixelSize: root.scaledPixelSize(Theme.text.caption)
        }
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 0
            Layout.topMargin: root.compact ? 0 : 8
            IncomingTransactionsChart {
                id: chart
                anchors.fill: parent
                labelFont: Qt.font({family: Theme.text.caption.family,
                                    styleName: Theme.text.caption.styleName,
                                    pixelSize: Math.round(Theme.text.caption.pixelSize * 0.8)})
                samples: root.samples
                capacityBaseline: root.capacityBaseline
                expanded: root.expanded
                showAxes: !root.compact
                active: root.active
                visible: root.ready && hasSamples
            }
            CoreText {
                objectName: "mempoolChartStatus"
                anchors.centerIn: parent
                width: parent.width
                visible: !root.ready || !chart.hasSamples
                text: !root.available ? qsTr("Unavailable in blocks-only mode")
                    : !root.ready ? qsTr("Waiting for node") : qsTr("Collecting incoming transactions…")
                font: root.scaledFont(Theme.text.caption)
                color: Theme.color.neutral7
                maximumLineCount: 2
                elide: Text.ElideRight
            }
        }
    }
}

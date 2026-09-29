// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15
import "../../controls"

DashboardWidget {
    id: root
    objectName: "mempoolSummaryWidget"
    property var activityModel: typeof mempoolActivityModel !== "undefined" ? mempoolActivityModel : null
    property var nodeModelRef: typeof nodeModel !== "undefined" ? nodeModel : null
    readonly property bool available: nodeModelRef && nodeModelRef.mempoolInformationAvailable
    readonly property bool ready: available && activityModel && activityModel.ready && activityModel.queuedVbytes >= 0
    readonly property int queuedBlocks: ready ? Math.ceil(activityModel.queuedVbytes / 1000000) : -1
    readonly property real memoryFraction: ready && nodeModelRef.mempoolMaxUsageMB > 0
        ? Math.max(0, Math.min(1, nodeModelRef.mempoolUsageMB / nodeModelRef.mempoolMaxUsageMB)) : 0
    Binding { target: root.activityModel; property: "summaryActive"; value: root.active && root.available; when: root.activityModel !== null }
    Component.onDestruction: if (activityModel) activityModel.summaryActive = false
    function number(value, decimals) { return Number(value).toLocaleString(Qt.locale(), 'f', decimals) }
    function feeText() {
        if (!ready || activityModel.minimumFee < 0) return "—"
        const rate = activityModel.minimumFee
        const perK = Math.round(rate * 1000)
        return qsTr("%1 sat/vB").arg(number(rate, perK % 1000 === 0 ? 0 : perK % 100 === 0 ? 1 : perK % 10 === 0 ? 2 : 3))
    }
    Accessible.description: qsTr("Unconfirmed transactions, estimated queued blocks, minimum fee and memory usage. Queued blocks use virtual size and do not predict confirmation time.")
    FontMetrics { id: countMetrics; font: count.font }
    FontMetrics { id: blockLabelMetrics; font: root.primaryLabelFont }
    ColumnLayout {
        anchors.fill: parent
        spacing: root.expanded ? 14 * root.fontScale : 6
        WidgetTitle { text: qsTr("Mempool") }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            ColumnLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.preferredWidth: root.width * 0.7
                Layout.alignment: root.expanded ? Qt.AlignBottom : Qt.AlignVCenter
                spacing: 2
                Item {
                    Layout.fillWidth: true
                    implicitHeight: count.height
                    CoreText {
                        id: count
                        objectName: "mempoolSummaryCount"
                        width: inlineCountLabel.visible
                            ? Math.max(0, Math.min(Math.ceil(countMetrics.advanceWidth(text)), parent.width - inlineCountLabel.implicitWidth - 6))
                            : parent.width
                        text: root.ready ? root.number(root.nodeModelRef.mempoolTransactionCount, 0) : "—"
                        font: root.primaryValueFont
                        horizontalAlignment: Text.AlignLeft
                        fontSizeMode: Text.Fit
                        minimumPixelSize: root.scaledPixelSize(root.secondaryValueStyle)
                        wrap: false
                    }
                    CoreText {
                        id: inlineCountLabel
                        objectName: "mempoolSummaryInlineCountLabel"
                        visible: !root.compact && !root.expanded
                        x: count.width + 6
                        anchors.baseline: count.baseline
                        text: qsTr("unconfirmed")
                        font: root.primaryLabelFont
                        color: Theme.color.neutral6
                        wrap: false
                    }
                }
                CoreText {
                    objectName: "mempoolSummaryCountLabel"
                    visible: root.compact || root.expanded
                    Layout.fillWidth: true
                    text: root.expanded ? qsTr("Unconfirmed transactions") : qsTr("Unconfirmed")
                    font: root.primaryLabelFont
                    color: Theme.color.neutral6
                    horizontalAlignment: Text.AlignLeft
                    wrap: false
                    elide: Text.ElideRight
                }
            }
            ColumnLayout {
                visible: !root.compact
                Layout.preferredWidth: root.expanded ? Math.ceil(blockLabelMetrics.advanceWidth(qsTr("Blocks queued")))
                    : Math.min(Math.ceil(blockLabelMetrics.advanceWidth(qsTr("Blocks queued"))),
                        Math.max(Math.ceil(blockLabelMetrics.advanceWidth(qsTr("Blocks"))),
                            root.width - Math.ceil(countMetrics.advanceWidth(count.text))
                            - Math.ceil(blockLabelMetrics.advanceWidth(qsTr("unconfirmed"))) - 20))
                Layout.minimumWidth: Layout.preferredWidth
                Layout.alignment: root.expanded ? Qt.AlignBottom : Qt.AlignVCenter
                spacing: 2
                CoreText {
                    objectName: "mempoolQueuedBlocks"
                    Layout.fillWidth: true
                    text: root.queuedBlocks >= 0 ? qsTr("≈%1").arg(root.number(root.queuedBlocks, 0)) : "—"
                    font: root.secondaryValueFont
                    horizontalAlignment: Text.AlignRight
                    wrap: false
                }
                CoreText {
                    objectName: "mempoolQueuedBlocksLabel"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    text: blockLabelMetrics.advanceWidth(qsTr("Blocks queued")) <= width ? qsTr("Blocks queued") : qsTr("Blocks")
                    font: root.primaryLabelFont
                    color: Theme.color.neutral6
                    horizontalAlignment: Text.AlignRight
                    wrap: false
                    elide: Text.ElideRight
                }
            }
        }
        Item {
            objectName: "mempoolSummaryTableArea"
            Layout.fillWidth: true
            Layout.fillHeight: root.expanded
            implicitHeight: table.implicitHeight
            WidgetSection {
                id: table
                objectName: "mempoolSummaryTable"
                anchors.bottom: parent.bottom
                width: parent.width
                height: root.expanded ? Math.max(implicitHeight, parent.height * 0.75) : implicitHeight
                padding: root.compact ? 5 : root.expanded ? 12 * root.fontScale : 8
                contentItem: ColumnLayout {
                    spacing: root.expanded ? 14 : 4
                    RowLayout {
                        visible: root.compact
                        Layout.fillWidth: true
                        spacing: 4
                        CoreText { text: qsTr("Min fee"); font: root.footerLabelFont; color: Theme.color.neutral6; wrap: false }
                        CoreText {
                            objectName: "mempoolSummaryCompactFee"
                            fontSizeMode: Text.Fit
                            minimumPixelSize: 9
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            text: root.feeText()
                            font: root.footerValueFont
                            horizontalAlignment: Text.AlignRight
                            wrap: false
                            elide: Text.ElideRight
                        }
                    }
                    GridLayout {
                        visible: !root.compact
                        Layout.fillWidth: true
                        Layout.fillHeight: root.expanded
                        columns: 2
                        rowSpacing: root.expanded ? 10 : 4
                        columnSpacing: 8
                        CoreText { Layout.fillHeight: root.expanded; Layout.row: 0; Layout.column: 0; text: qsTr("Min fee"); font: root.footerLabelFont; color: Theme.color.neutral6 }
                        CoreText {
                            Layout.row: root.expanded ? 0 : 1
                            Layout.column: root.expanded ? 1 : 0
                            Layout.fillHeight: root.expanded
                            objectName: "mempoolSummaryFee"
                            text: root.feeText()
                            font: root.footerValueFont
                            horizontalAlignment: root.expanded ? Text.AlignRight : Text.AlignLeft
                            Layout.fillWidth: true
                            wrap: false
                            elide: Text.ElideRight
                        }
                        Rectangle {
                            objectName: "mempoolSummaryDivider"
                            visible: root.expanded
                            Layout.row: 1
                            Layout.column: 0
                            Layout.columnSpan: 2
                            Layout.fillWidth: true
                            Layout.preferredHeight: 1
                            color: Theme.color.neutral3
                        }
                        CoreText { Layout.fillHeight: root.expanded; Layout.row: root.expanded ? 2 : 0; Layout.column: root.expanded ? 0 : 1; text: qsTr("Memory"); font: root.footerLabelFont; color: Theme.color.neutral6; horizontalAlignment: root.expanded ? Text.AlignLeft : Text.AlignRight; Layout.fillWidth: true }
                        CoreText {
                            Layout.row: root.expanded ? 2 : 1
                            Layout.column: 1
                            Layout.fillHeight: root.expanded
                            objectName: "mempoolSummaryMemory"
                            text: root.ready ? qsTr("%1 / %2 MB").arg(root.number(root.nodeModelRef.mempoolUsageMB, 2)).arg(root.number(root.nodeModelRef.mempoolMaxUsageMB, 0)) : "—"
                            font: root.footerValueFont
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignRight
                            wrap: false
                            elide: Text.ElideRight
                        }
                    }
                    Rectangle {
                        visible: root.expanded
                        Layout.fillWidth: true
                        Layout.preferredHeight: 6
                        radius: 3
                        color: Theme.color.neutral3
                        Rectangle { width: parent.width * root.memoryFraction; height: parent.height; radius: 3; color: root.memoryFraction >= 0.9 ? Theme.color.red : Theme.color.green }
                    }
                }
            }
        }
    }
}

// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15
import "../../controls"

DashboardWidget {
    id: root
    objectName: "halvingWidget"
    property var milestoneModel: typeof halvingModel !== "undefined" ? halvingModel : null
    readonly property bool available: milestoneModel && milestoneModel.available
    readonly property real progress: available ? milestoneModel.progress : 0
    readonly property int percent: Math.floor(progress * 100)
    function number(value, decimals) { return Number(value).toLocaleString(Qt.locale(), 'f', decimals) }
    function subsidy(value) { return number(value, 8).replace(/0+$/, "").replace(/[.,]$/, "") }
    function arrival() {
        if (!available) return "—"
        if (milestoneModel.complete) return qsTr("Subsidy complete")
        const days = Math.floor(milestoneModel.secondsRemaining / 86400)
        if (days >= 365) return qsTr("In ≈%1y %2d").arg(number(Math.floor(days / 365), 0)).arg(number(days % 365, 0))
        if (days > 0) return qsTr("In ≈%1d").arg(number(days, 0))
        return qsTr("In ≈%1h").arg(number(Math.max(1, Math.ceil(milestoneModel.secondsRemaining / 3600)), 0))
    }
    Accessible.description: qsTr("Halving cycle progress and remaining blocks. Estimated arrival uses the network target block interval.")
    ColumnLayout {
        anchors.fill: parent
        spacing: root.expanded ? 12 : 6
        WidgetTitle { Layout.fillWidth: true; text: qsTr("Halving progress") }
        Item { visible: root.expanded; Layout.fillHeight: true }
        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                CoreText {
                    objectName: "halvingHeadline"
                    Layout.fillWidth: true
                    text: root.available ? qsTr("%1%").arg(root.percent) : "—"
                    font: root.primaryValueFont
                    color: Theme.color.orange
                    horizontalAlignment: Text.AlignLeft
                    wrap: false
                    fontSizeMode: Text.Fit
                    minimumPixelSize: root.scaledPixelSize(root.secondaryValueStyle)
                }
                CoreText { visible: root.compact; text: root.compact && root.available ? qsTr("%1 left").arg(root.number(root.milestoneModel.blocksLeft, 0)) : qsTr("Blocks remaining"); font: root.primaryLabelFont; color: Theme.color.neutral6; Layout.fillWidth: true; horizontalAlignment: Text.AlignLeft; wrap: false; elide: Text.ElideRight }
            }
            ColumnLayout {
                visible: !root.compact
                spacing: 2
                CoreText {
                    objectName: "halvingBlocksRemaining"
                    Layout.fillWidth: true
                    text: root.available ? root.number(root.milestoneModel.blocksLeft, 0) : "—"
                    font: root.secondaryValueFont
                    color: Theme.color.neutral9
                    horizontalAlignment: Text.AlignRight
                    wrap: false
                }
                CoreText {
                    text: qsTr("Blocks remaining")
                    font: root.primaryLabelFont
                    color: Theme.color.neutral6
                    horizontalAlignment: Text.AlignRight
                    Layout.fillWidth: true
                    wrap: false
                }
            }
        }
        Rectangle {
            objectName: "halvingProgressBar"
            Layout.fillWidth: true
            Layout.preferredHeight: root.expanded ? 10 : 6
            radius: height / 2
            color: Theme.color.neutral2
            Rectangle {
                objectName: "halvingProgressFill"
                width: parent.width * Math.max(0, Math.min(1, root.progress))
                height: parent.height
                radius: parent.radius
                color: Theme.color.orange
            }
        }
        RowLayout {
            visible: root.expanded
            Layout.fillWidth: true
            CoreText { text: root.available ? root.number(root.milestoneModel.periodStart, 0) : "—"; font: root.footerLabelFont; color: Theme.color.neutral6 }
            Item { Layout.fillWidth: true }
            CoreText { text: root.available && !root.milestoneModel.complete ? root.number(root.milestoneModel.nextHeight, 0) : "—"; font: root.footerLabelFont; color: Theme.color.neutral6 }
        }
        Item { visible: root.expanded; Layout.fillHeight: true }
        WidgetSection {
            objectName: "halvingDetailsSection"
            Layout.preferredHeight: implicitHeight * (root.expanded ? 1.4 : 1)
            visible: !root.compact
            Layout.fillWidth: true
            padding: root.expanded ? 12 : 0
            background: Rectangle { radius: 12; color: root.expanded ? Theme.color.neutral1 : "transparent" }
            contentItem: RowLayout {
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    CoreText { visible: root.expanded; text: qsTr("Estimated arrival"); font: root.footerLabelFont; color: Theme.color.neutral6 }
                    CoreText {
                        objectName: "halvingArrival"
                        text: root.arrival()
                        font: root.footerValueFont
                        color: Theme.color.neutral9
                        wrap: false
                    }
                }
                ColumnLayout {
                    visible: root.expanded
                    spacing: 4
                    CoreText { Layout.fillWidth: true; text: qsTr("Block subsidy · BTC"); font: root.footerLabelFont; color: Theme.color.neutral6; horizontalAlignment: Text.AlignRight }
                    CoreText { objectName: "halvingSubsidy"; Layout.fillWidth: true; text: root.available ? qsTr("%1 → %2").arg(root.subsidy(root.milestoneModel.subsidy)).arg(root.subsidy(root.milestoneModel.nextSubsidy)) : "—"; font: root.footerValueFont; horizontalAlignment: Text.AlignRight; wrap: false; elide: Text.ElideRight }
                }
            }
        }
    }
}

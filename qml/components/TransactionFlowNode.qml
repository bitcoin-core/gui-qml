// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../controls"

Rectangle {
    id: root
    property var entry: ({})
    property string walletName: qsTr("Your wallet")
    property color accentColor: Theme.color.purple
    property bool input: false
    property bool interactive: true
    property bool selected: false
    readonly property bool compact: width < 340
    readonly property var requests: entry.paymentRequests || []
    readonly property bool hasData: entry.kind === "data" && entry.dataType === "OP_RETURN"
    readonly property string dataText: {
        if (entry.dataText !== undefined && entry.dataText !== null) return entry.dataText || qsTr("No data")
        if (entry.dataHex !== undefined && entry.dataHex !== null) return qsTr("Hex: %1").arg(entry.dataHex)
        return qsTr("Script (hex): %1").arg(entry.scriptHex || "")
    }
    readonly property string title: entry.label || (entry.kind === "coinbase" ? qsTr("Block reward")
        : entry.kind === "fee" ? qsTr("Network fee")
        : entry.kind === "output-group" ? qsTr("%1 outputs").arg(entry.outputCount)
        : entry.kind === "data" ? qsTr("Data output")
        : entry.ownership === "wallet" ? (entry.isChange ? qsTr("Change · %1").arg(walletName) : walletName)
        : input ? qsTr("External input") : qsTr("Recipient"))
    readonly property string amountText: entry.amountKnown ? (entry.amount || String(entry.amountSat) + " " + qsTr("sats")) : qsTr("Unknown amount")
    signal paymentRequestRequested(string requestId)

    color: Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.2)
    border.width: 2
    border.color: Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.6)
    radius: 10
    implicitHeight: Math.max(80, content.implicitHeight + 24)
    height: implicitHeight
    Accessible.role: Accessible.Grouping
    Accessible.name: title + ", " + amountText

    ColumnLayout {
        id: content
        x: 16
        y: 12
        width: parent.width - 32
        spacing: 6
        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            CoreText {
                Layout.fillWidth: true
                text: root.title
                textFormat: Text.PlainText
                color: root.accentColor
                font: Theme.text.captionStrong.font
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignLeft
            }
            CoreText {
                visible: !root.compact
                text: root.amountText
                font: Theme.text.monoCaption.font
            }
        }
        CoreText {
            visible: root.compact
            Layout.fillWidth: true
            text: root.amountText
            font: Theme.text.monoCaption.font
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignLeft
        }
        AddressLabel {
            visible: !!root.entry.address
            Layout.fillWidth: true
            address: root.entry.address || ""
            truncated: true
            leadingCharacterCount: root.width < 240 ? 4 : 8
            trailingCharacterCount: root.width < 240 ? 4 : 8
            embedded: true
            interactive: root.interactive
            textStyle: Theme.text.monoCaption
            leftPadding: 0
            rightPadding: 0
            topPadding: 0
            bottomPadding: 0
        }
        CoreText {
            visible: !root.entry.address && !root.hasData
            Layout.fillWidth: true
            text: root.entry.kind === "fee" ? qsTr("Total transaction fee")
                : root.entry.kind === "output-group" ? qsTr("Not wallet-owned")
                : root.entry.kind === "coinbase" ? qsTr("Newly mined bitcoin")
                : root.entry.previousTxid ? String(root.entry.previousTxid).substring(0, 8) + "…:" + root.entry.previousOutputIndex
                : qsTr("No standard address")
            font: Theme.text.caption.font
            color: Theme.color.neutral7
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignLeft
        }
        Rectangle {
            visible: root.hasData
            objectName: "transactionFlowDataTypePill"
            Layout.alignment: Qt.AlignLeft
            implicitWidth: dataTypeLabel.implicitWidth + 20
            implicitHeight: dataTypeLabel.implicitHeight + 8
            radius: height / 2
            color: Theme.color.neutral3
            CoreText {
                id: dataTypeLabel
                anchors.centerIn: parent
                text: root.entry.dataType || ""
                font: Theme.text.captionStrong.font
                color: Theme.color.neutral8
                textFormat: Text.PlainText
            }
        }
        CoreText {
            objectName: "transactionFlowDataText"
            visible: root.hasData
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            text: root.dataText
            textFormat: Text.PlainText
            font: Theme.text.caption.font
            color: Theme.color.neutral7
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
        }
        Repeater {
            model: root.requests
            delegate: AbstractButton {
                required property var modelData
                Layout.fillWidth: true
                implicitHeight: 36
                enabled: root.interactive
                Accessible.name: qsTr("View associated payment request")
                onClicked: root.paymentRequestRequested(String(modelData.requestId || ""))
                background: Rectangle {
                    color: Qt.rgba(Theme.color.lavender.r, Theme.color.lavender.g, Theme.color.lavender.b, 0.18)
                    radius: 6
                }
                contentItem: RowLayout {
                    spacing: 6
                    Icon { source: "qrc:/icons/activity-payment-request.svg"; color: Theme.color.lavender; size: 16 }
                    CoreText {
                        Layout.fillWidth: true
                        text: qsTr("Payment request #%1").arg(modelData.requestId || "")
                        color: Theme.color.lavender
                        font: Theme.text.caption.font
                        horizontalAlignment: Text.AlignLeft
                        elide: Text.ElideRight
                    }
                    CoreText { text: "→"; color: Theme.color.lavender; font: Theme.text.caption.font }
                }
                leftPadding: 8
                rightPadding: 8
            }
        }
    }
}

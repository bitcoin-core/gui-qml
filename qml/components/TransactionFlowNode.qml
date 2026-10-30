// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0
import "../controls"

Rectangle {
    id: root
    property var entry: ({})
    property int displayUnit: optionsModel.displayUnit
    property string walletName: qsTr("Your wallet")
    property color accentColor: Theme.color.txInternal
    property bool input: false
    property bool interactive: true
    property bool selected: false
    property bool hidesAmount: false
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
    readonly property string amountText: entry.amountKnown ? nodeAmount.localizedDisplayWithUnit : qsTr("Unknown amount")
    signal paymentRequestRequested(string requestId)

    BitcoinAmount {
        id: nodeAmount
        unit: root.displayUnit
        satoshi: root.entry.amountKnown ? root.entry.amountSat : 0
    }

    color: Theme.color.tint(accentColor)
    border.width: 2
    border.color: Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.6)
    radius: Theme.radius.cardInset
    implicitHeight: Math.max(80, content.height + 24)
    height: implicitHeight
    Accessible.role: Accessible.Grouping
    Accessible.name: title + ", " + (hidesAmount && optionsModel.hideAmounts ? qsTr("Hidden amount") : amountText)

    ColumnLayout {
        id: content
        x: 16
        y: 12
        width: parent.width - 32
        spacing: Theme.space.s
        Item {
            Layout.fillWidth: true
            implicitHeight: Math.max(flowTitle.implicitHeight, flowAmount.visible ? flowAmount.implicitHeight : 0)
            CoreText {
                id: flowTitle
                objectName: "transactionFlowTitle"
                width: Math.max(0, parent.width - (flowAmount.visible ? flowAmount.width + Theme.space.ml : 0))
                height: parent.height
                text: root.title
                textFormat: Text.PlainText
                color: root.accentColor
                textStyle: Theme.text.captionStrong
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignLeft
            }
            BitcoinAmountDisplayLabel {
                id: flowAmount
                objectName: "transactionFlowAmountWide"
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: Math.min(implicitWidth, parent.width)
                displayUnit: root.displayUnit
                visible: !root.compact
                // Amounts being reviewed or confirmed never roll through other digits.
                animateUnitChanges: false
                hidesAmount: root.hidesAmount
                text: root.amountText
                textStyle: Theme.text.amountCaption
            }
        }
        BitcoinAmountDisplayLabel {
            objectName: "transactionFlowAmountCompact"
            displayUnit: root.displayUnit
            visible: root.compact
            animateUnitChanges: false
            hidesAmount: root.hidesAmount
            Layout.fillWidth: true
            text: root.amountText
            textStyle: Theme.text.amountCaption
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignLeft
        }
        AddressLabel {
            objectName: "transactionFlowAddress_" + root.entry.id
            visible: !!root.entry.address
            Layout.fillWidth: true
            address: root.entry.address || ""
            truncated: true
            leadingCharacterCount: root.width < 240 ? 4 : 8
            trailingCharacterCount: root.width < 240 ? 4 : 8
            surfaceLevel: 2
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
            textStyle: Theme.text.caption
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
            color: Theme.color.surface3
            CoreText {
                id: dataTypeLabel
                anchors.centerIn: parent
                text: root.entry.dataType || ""
                textStyle: Theme.text.captionStrong
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
            textStyle: Theme.text.caption
            color: Theme.color.neutral7
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
        }
        // A positioner keeps Repeater delegates out of the Qt 6.4 layout engine.
        Column {
            Layout.fillWidth: true
            visible: root.requests.length > 0
            spacing: Theme.space.s

            Repeater {
                model: root.requests
                delegate: AbstractButton {
                    id: requestButton
                    required property var modelData
                    objectName: "transactionFlowRequest_" + modelData.requestId
                    width: parent ? parent.width : 0
                    implicitHeight: Theme.size.control
                    enabled: root.interactive
                    Accessible.name: qsTr("View associated payment request")
                    onClicked: root.paymentRequestRequested(String(modelData.requestId || ""))
                    background: Rectangle {
                        color: Theme.color.paymentRequestTint
                        radius: 6

                        FocusBorder {
                            visible: requestButton.visualFocus
                            targetRadius: parent.radius
                        }
                    }
                    contentItem: RowLayout {
                        spacing: Theme.space.s
                        Icon { source: "qrc:/icons/activity-payment-request.svg"; color: Theme.color.paymentRequest; size: 16 }
                        CoreText {
                            Layout.fillWidth: true
                            text: qsTr("Payment request #%1").arg(modelData.requestId || "")
                            color: Theme.color.paymentRequestText
                            textStyle: Theme.text.caption
                            horizontalAlignment: Text.AlignLeft
                            elide: Text.ElideRight
                        }
                        CoreText { text: "→"; color: Theme.color.paymentRequestText; textStyle: Theme.text.caption }
                    }
                    leftPadding: Theme.space.m
                    rightPadding: Theme.space.m
                }
            }
        }
    }
}

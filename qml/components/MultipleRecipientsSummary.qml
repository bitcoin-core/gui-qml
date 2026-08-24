// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

import "../controls"

ColumnLayout {
    id: root
    objectName: "multipleRecipientsSummary"

    property WalletQmlModel wallet: walletController.selectedWallet
    property WalletQmlModelTransaction transaction: wallet ? wallet.currentTransaction : null

    spacing: 15

    Separator {
        Layout.fillWidth: true
    }

    ListView {
        id: inputsList
        objectName: "multipleSendReviewRecipientsList"
        Layout.fillWidth: true
        Layout.preferredHeight: contentHeight
        model: root.wallet ? root.wallet.recipients : null
        clip: true
        interactive: false

        delegate: Item {
            id: delegate
            implicitHeight: delegateColumn.implicitHeight
            height: implicitHeight
            width: ListView.view.width

            required property string address;
            required property string label;
            required property string amount;
            required property string formattedAddress;
            required property string amountUnitLabel;
            required property bool isDataOutput;
            required property string dataHex;
            property bool expanded: false
            readonly property bool expandable: formattedAddress.length > 0
            readonly property string amountText: amountUnitLabel.length > 0 ? amount + " " + amountUnitLabel : amount
            readonly property string primaryText: isDataOutput ? qsTr("Data output") : (label.length > 0 ? label : address)
            readonly property string secondaryText: {
                if (isDataOutput) return dataHex
                if (expanded) return formattedAddress
                return label.length > 0 ? address : ""
            }
            readonly property bool secondaryVisible: secondaryText.length > 0

            activeFocusOnTab: expandable
            Accessible.role: Accessible.Button
            Accessible.name: isDataOutput
                ? qsTr("Data output %1, amount %2").arg(dataHex).arg(amountText)
                : label.length > 0
                    ? qsTr("%1, address %2, amount %3").arg(label).arg(formattedAddress).arg(amountText)
                    : qsTr("Address %1, amount %2").arg(formattedAddress).arg(amountText)
            Accessible.description: expanded ? qsTr("Hide full address") : qsTr("Show full address")
            Accessible.onPressAction: click()

            function click() {
                if (expandable) {
                    expanded = !expanded
                }
            }

            Keys.onPressed: (event) => {
                if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) {
                    delegate.click()
                    event.accepted = true
                }
            }

            onExpandableChanged: {
                if (!expandable) {
                    expanded = false
                }
            }

            Column {
                id: delegateColumn
                width: parent.width
                spacing: 0

                Item {
                    width: 1
                    height: 15
                }

                RowLayout {
                    width: parent.width
                    spacing: 10

                    HoverHandler {
                        enabled: delegate.expandable
                        cursorShape: Qt.PointingHandCursor
                    }

                    TapHandler {
                        enabled: delegate.expandable
                        onTapped: delegate.click()
                    }

                    CoreText {
                        objectName: "multipleSendReviewRecipient" + index + "PrimaryText"
                        Layout.fillWidth: true
                        Layout.preferredWidth: 0
                        horizontalAlignment: Text.AlignLeft
                        verticalAlignment: Text.AlignTop
                        wrap: false
                        elide: Text.ElideRight
                        text: delegate.primaryText
                        font: Theme.text.description.font
                        lineHeight: Theme.text.description.lineHeight
                        lineHeightMode: Text.FixedHeight
                        color: Theme.color.neutral9
                    }

                    Item {
                        id: amountDisplay
                        objectName: "multipleSendReviewRecipient" + index + "Amount"
                        property string text: delegate.amountText
                        implicitWidth: amountRow.implicitWidth
                        implicitHeight: amountRow.implicitHeight
                        Layout.alignment: Qt.AlignRight | Qt.AlignTop

                        RowLayout {
                            id: amountRow
                            anchors.fill: parent
                            spacing: 5

                            CoreText {
                                text: delegate.amount
                                font: Theme.text.description.font
                                lineHeight: Theme.text.description.lineHeight
                                lineHeightMode: Text.FixedHeight
                                wrap: false
                                color: Theme.color.neutral9
                            }

                            CoreText {
                                visible: delegate.amountUnitLabel.length > 0
                                text: delegate.amountUnitLabel
                                font: Theme.text.description.font
                                lineHeight: Theme.text.description.lineHeight
                                lineHeightMode: Text.FixedHeight
                                wrap: false
                                color: Theme.color.neutral9
                            }
                        }
                    }
                }

                Item {
                    width: 1
                    height: 10
                    visible: delegate.secondaryVisible
                }

                TextArea {
                    objectName: "multipleSendReviewRecipient" + index + "SecondaryText"
                    width: parent.width
                    visible: delegate.secondaryVisible
                    readOnly: true
                    selectByMouse: true
                    text: delegate.secondaryText
                    wrapMode: Text.WordWrap
                    leftPadding: 0
                    topPadding: 0
                    rightPadding: 0
                    bottomPadding: 0
                    height: visible ? Math.max(contentHeight, 21) : 0
                    font: Theme.text.description.font
                    color: Theme.color.neutral9
                    background: Item {}
                }

                Item {
                    width: 1
                    height: 15
                    visible: index < ListView.view.count - 1
                }

                Separator {
                    width: parent.width
                    visible: index < ListView.view.count - 1
                }
            }

            FocusBorder {
                visible: delegate.activeFocus
                topMargin: 0
                bottomMargin: 0
                leftMargin: -4
                rightMargin: -4
            }
        }
    }

    BitcoinAmountDisplayField {
        objectName: "multipleSendReviewFeeField"
        Layout.topMargin: 10
        labelText: qsTr("Fee")
        labelPixelSize: Theme.text.description.pixelSize
        labelColor: Theme.color.neutral7
        amountText: root.transaction ? root.transaction.feeAmount.display : ""
        unitText: root.transaction ? root.transaction.feeAmount.unitLabel : ""
    }

    Separator {
        Layout.fillWidth: true
    }

    BitcoinAmountDisplayField {
        objectName: "multipleSendReviewTotalField"
        labelWidth: 130
        labelText: qsTr("Total amount")
        amountText: root.transaction ? root.transaction.totalAmount.display : ""
        unitText: root.transaction ? root.transaction.totalAmount.unitLabel : ""
    }
}

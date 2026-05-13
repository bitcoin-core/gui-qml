// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

import "../../controls"
import "../../components"

Page {
    id: root
    objectName: "multipleSendReviewPage"
    background: null

    property WalletQmlModel wallet: walletController.selectedWallet
    property WalletQmlModelTransaction transaction: walletController.selectedWallet.currentTransaction
    property bool sending: false
    readonly property int recipientCount: root.wallet ? root.wallet.recipients.count : 0
    readonly property string recipientCountText: recipientCount === 1
        ? qsTr("There is 1 recipient.")
        : qsTr("There are %1 recipients.").arg(recipientCount)

    signal finished()
    signal back()
    signal transactionSent()

    onVisibleChanged: {
        if (!visible) {
            externalSignerActions.reset()
        }
    }

    header: NavigationBar2 {
        id: navbar
        leftItem: NavButton {
            objectName: "multipleSendReviewBackButton"
            iconSource: "image://images/caret-left"
            text: root.wallet && root.wallet.hasExternalSigner ? qsTr("Edit") : qsTr("Back")
            onClicked: {
                externalSignerActions.reset()
                root.back()
            }
        }
    }

    ScrollView {
        clip: true
        width: parent.width
        height: parent.height
        contentWidth: width

        ColumnLayout {
            id: columnLayout
            width: 450
            anchors.horizontalCenter: parent.horizontalCenter

            spacing: 15

            ColumnLayout {
                Layout.topMargin: 30
                Layout.fillWidth: true
                spacing: 5

                CoreText {
                    id: title
                    Layout.fillWidth: true
                    text: qsTr("Review transaction")
                    horizontalAlignment: Text.AlignLeft
                    font.pixelSize: 21
                    bold: true
                }

                CoreText {
                    objectName: "multipleSendReviewRecipientCountText"
                    Layout.fillWidth: true
                    text: root.recipientCountText
                    horizontalAlignment: Text.AlignLeft
                    font.pixelSize: 13
                    color: Theme.color.neutral7
                }
            }

            Separator {
                Layout.fillWidth: true
            }

            ListView {
                id: inputsList
                objectName: "multipleSendReviewRecipientsList"
                Layout.fillWidth: true
                Layout.preferredHeight: contentHeight
                model: root.wallet.recipients
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
                    property bool expanded: false
                    readonly property bool expandable: formattedAddress.length > 0
                    readonly property string amountText: amountUnitLabel.length > 0 ? amount + " " + amountUnitLabel : amount
                    readonly property string primaryText: label.length > 0 ? label : address
                    readonly property string secondaryText: expanded ? formattedAddress : ""
                    readonly property bool secondaryVisible: secondaryText.length > 0

                    function click() {
                        if (expandable) {
                            expanded = !expanded
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

                            CoreText {
                                objectName: "multipleSendReviewRecipient" + index + "PrimaryText"
                                Layout.fillWidth: true
                                Layout.preferredWidth: 0
                                horizontalAlignment: Text.AlignLeft
                                verticalAlignment: Text.AlignTop
                                wrap: false
                                elide: Text.ElideRight
                                text: primaryText
                                font.pixelSize: 15
                                color: Theme.color.neutral9

                                HoverHandler {
                                    enabled: delegate.expandable
                                    cursorShape: Qt.PointingHandCursor
                                }

                                TapHandler {
                                    enabled: delegate.expandable
                                    onTapped: delegate.expanded = !delegate.expanded
                                }
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
                                        text: amount
                                        font.pixelSize: 15
                                        wrap: false
                                        color: Theme.color.neutral9
                                    }

                                    CoreText {
                                        visible: amountUnitLabel.length > 0
                                        text: amountUnitLabel
                                        font.pixelSize: 15
                                        wrap: false
                                        color: Theme.color.neutral9
                                    }
                                }
                            }
                        }

                        Item {
                            width: 1
                            height: 10
                            visible: secondaryText.length > 0
                        }

                        TextArea {
                            objectName: "multipleSendReviewRecipient" + index + "SecondaryText"
                            width: parent.width
                            visible: secondaryText.length > 0
                            readOnly: true
                            selectByMouse: true
                            text: secondaryText
                            wrapMode: Text.WordWrap
                            leftPadding: 0
                            topPadding: 0
                            rightPadding: 0
                            bottomPadding: 0
                            height: visible ? Math.max(contentHeight, 21) : 0
                            font.family: "BitcoinCoreSans"
                            font.styleName: "Regular"
                            font.pixelSize: 15
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
                }
            }

            BitcoinAmountDisplayField {
                objectName: "multipleSendReviewFeeField"
                Layout.topMargin: 10
                labelText: qsTr("Fee")
                labelPixelSize: 15
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

            ExternalSignerReviewActions {
                id: externalSignerActions
                visible: root.wallet && root.wallet.hasExternalSigner
                wallet: root.wallet
                buttonObjectName: "multipleSendReviewExternalSignerButton"
                statusObjectName: "multipleSendReviewStatusText"
                Layout.fillWidth: true
                Layout.topMargin: 30
                onSendRequested: {
                    if (root.sending) {
                        return
                    }
                    if (root.wallet.sendTransaction()) {
                        root.sending = true
                        root.transactionSent()
                    }
                }
            }

            ContinueButton {
                id: confirmationButton
                objectName: "sendReviewSendButton"
                visible: !root.wallet || !root.wallet.hasExternalSigner
                enabled: !root.sending
                Layout.fillWidth: true
                Layout.topMargin: 30
                text: qsTr("Send")
                onClicked: {
                    if (root.sending) {
                        return
                    }
                    if (root.wallet.sendTransaction()) {
                        root.sending = true
                        root.transactionSent()
                    }
                }
            }

            CoreText {
                objectName: "sendReviewErrorText"
                Layout.fillWidth: true
                visible: text.length > 0
                text: root.wallet.transactionError
                color: Theme.color.red
                font.pixelSize: 15
                wrapMode: Text.WordWrap
            }
        }
    }
}

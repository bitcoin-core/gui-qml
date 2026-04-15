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
    objectName: "sendReviewPage"
    background: null

    property WalletQmlModel wallet: walletController.selectedWallet
    property SendRecipient recipient: wallet.recipients.current
    property WalletQmlModelTransaction transaction: walletController.selectedWallet.currentTransaction
    property bool sending: false

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
            objectName: "sendReviewBackButton"
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

            spacing: 10

            CoreText {
                id: title
                Layout.topMargin: 30
                Layout.bottomMargin: 20
                text: qsTr("Review transaction")
                font.pixelSize: 21
                bold: true
            }

            BitcoinAddressDisplayField {
                objectName: "sendReviewAddressField"
                expandedObjectName: "sendReviewFullAddressField"
                labelPixelSize: 15
                labelColor: Theme.color.neutral7
                text: root.recipient ? root.recipient.address.ellipsesAddress : ""
                fullText: root.recipient ? root.recipient.address.formattedAddress : ""
            }

            LabeledValueField {
                id: noteField
                objectName: "sendReviewNoteField"
                visible: text.length > 0
                labelText: qsTr("Note")
                labelPixelSize: 15
                labelColor: Theme.color.neutral7
                valueHorizontalAlignment: Text.AlignRight
                text: root.recipient ? root.recipient.label : ""
            }

            BitcoinAmountDisplayField {
                objectName: "sendReviewAmountField"
                labelText: qsTr("Amount")
                labelPixelSize: 15
                labelColor: Theme.color.neutral7
                amountText: root.transaction ? root.transaction.amountAmount.display : ""
                unitText: root.transaction ? root.transaction.amountAmount.unitLabel : ""
            }

            BitcoinAmountDisplayField {
                objectName: "sendReviewFeeField"
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
                objectName: "sendReviewTotalField"
                labelWidth: 130
                labelText: qsTr("Total amount")
                amountText: root.transaction ? root.transaction.totalAmount.display : ""
                unitText: root.transaction ? root.transaction.totalAmount.unitLabel : ""
            }

            ExternalSignerReviewActions {
                id: externalSignerActions
                visible: root.wallet && root.wallet.hasExternalSigner
                wallet: root.wallet
                buttonObjectName: "sendReviewExternalSignerButton"
                statusObjectName: "sendReviewStatusText"
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
                objectName: "sendTransactionButton"
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
                objectName: "sendTransactionErrorText"
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

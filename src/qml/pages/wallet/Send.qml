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
    background: null

    property WalletQmlModel wallet: walletController.selectedWallet
    property SendRecipient recipient: wallet.sendRecipient

    signal transactionPrepared()

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
                text: qsTr("Send bitcoin")
                font.pixelSize: 21
                bold: true
            }

            LabeledTextInput {
                id: address
                Layout.fillWidth: true
                labelText: qsTr("Send to")
                placeholderText: qsTr("Enter address...")
                text: root.recipient.address
                onTextEdited: root.recipient.address = address.text
            }

            Separator {
                Layout.fillWidth: true
            }

            Item {
                BitcoinAmount {
                    id: bitcoinAmount
                }

                height: amountInput.height
                Layout.fillWidth: true
                CoreText {
                    id: amountLabel
                    width: 110
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    horizontalAlignment: Text.AlignLeft
                    color: Theme.color.neutral9
                    text: "Amount"
                    font.pixelSize: 18
                }

                TextField {
                    id: amountInput
                    anchors.left: amountLabel.right
                    anchors.verticalCenter: parent.verticalCenter
                    leftPadding: 0
                    font.family: "Inter"
                    font.styleName: "Regular"
                    font.pixelSize: 18
                    color: Theme.color.neutral9
                    placeholderTextColor: Theme.color.neutral7
                    background: Item {}
                    placeholderText: "0.00000000"
                    selectByMouse: true
                    onTextEdited: {
                        amountInput.text = bitcoinAmount.amount = bitcoinAmount.sanitize(amountInput.text)
                        root.recipient.amount = bitcoinAmount.satoshiAmount
                    }
                }
                Item {
                    width: unitLabel.width + flipIcon.width
                    height: Math.max(unitLabel.height, flipIcon.height)
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            if (bitcoinAmount.unit == BitcoinAmount.BTC) {
                                amountInput.text = bitcoinAmount.convert(amountInput.text, BitcoinAmount.BTC)
                                bitcoinAmount.unit = BitcoinAmount.SAT
                            } else {
                                amountInput.text = bitcoinAmount.convert(amountInput.text, BitcoinAmount.SAT)
                                bitcoinAmount.unit = BitcoinAmount.BTC
                            }
                        }
                    }
                    CoreText {
                        id: unitLabel
                        anchors.right: flipIcon.left
                        anchors.verticalCenter: parent.verticalCenter
                        text: bitcoinAmount.unitLabel
                        font.pixelSize: 18
                        color: Theme.color.neutral7
                    }
                    Icon {
                        id: flipIcon
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        source: "image://images/flip-vertical"
                        color: Theme.color.neutral8
                        size: 30
                    }
                }
            }

            Separator {
                Layout.fillWidth: true
            }

            LabeledTextInput {
                id: label
                Layout.fillWidth: true
                labelText: qsTr("Note to self")
                placeholderText: qsTr("Enter ...")
                onTextEdited: root.recipient.label = label.text
            }

            Separator {
                Layout.fillWidth: true
            }

            Item {
                height: feeLabel.height + feeValue.height
                Layout.fillWidth: true
                CoreText {
                    id: feeLabel
                    anchors.left: parent.left
                    anchors.top: parent.top
                    color: Theme.color.neutral9
                    text: "Fee"
                    font.pixelSize: 15
                }

                CoreText {
                    id: feeValue
                    anchors.right: parent.right
                    anchors.top: parent.top
                    color: Theme.color.neutral9
                    text: qsTr("Default (~2,000 sats)")
                    font.pixelSize: 15
                }
            }

            ContinueButton {
                id: continueButton
                Layout.fillWidth: true
                Layout.topMargin: 30
                text: qsTr("Review")
                onClicked: {
                    if (root.wallet.prepareTransaction()) {
                        root.transactionPrepared()
                    }
                }
            }
        }
    }
}

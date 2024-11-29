// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

import "../../controls"
import "../../components"
import "../settings"

PageStack {
    id: stackView
    initialItem: pageComponent

    Component {
        id: pageComponent
        Page {
            id: root
            background: null

            header: NavigationBar2 {
                id: navbar
                centerItem: Item {
                    id: header
                    Layout.fillWidth: true

                    CoreText {
                        anchors.left: parent.left
                        text: qsTr("Request a payment")
                        font.pixelSize: 21
                        bold: true
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
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: Math.min(parent.width, 450)
                    spacing: 30

                    CoreText {
                        Layout.alignment: Qt.AlignHCenter
                        text: qsTr("All fields are optional.")
                        color: Theme.color.neutral7
                        font.pixelSize: 15
                    }

                    LabeledTextInput {
                        id: label
                        Layout.fillWidth: true
                        labelText: qsTr("Label")
                        placeholderText: qsTr("Enter label...")
                    }

                    Item {
                        BitcoinAmount {
                            id: bitcoinAmount
                        }

                        height: 50
                        Layout.fillWidth: true
                        CoreText {
                            anchors.left: parent.left
                            anchors.top: parent.top
                            color: Theme.color.neutral7
                            text: "Amount"
                            font.pixelSize: 15
                        }

                        TextField {
                            id: amountInput
                            anchors.left: parent.left
                            anchors.bottom: parent.bottom
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
                                amountInput.text = bitcoinAmount.sanitize(amountInput.text)
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

                    LabeledTextInput {
                        id: message
                        Layout.fillWidth: true
                        labelText: qsTr("Message")
                        placeholderText: qsTr("Enter message...")
                    }

                    ContinueButton {
                        id: continueButton
                        Layout.preferredWidth: Math.min(300, parent.width - 2 * Layout.leftMargin)
                        Layout.leftMargin: 20
                        Layout.rightMargin: 20
                        Layout.alignment: Qt.AlignCenter
                        text: qsTr("Continue")
                        onClicked: stackView.push(confirmationComponent)
                    }
                }
            }
        }
    }

    Component {
        id: confirmationComponent
        RequestConfirmation {
        }
    }
}

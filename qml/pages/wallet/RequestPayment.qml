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

Page {
    id: root
    background: null

    property int requestCounter: 0

    ScrollView {
        clip: true
        width: parent.width
        height: parent.height
        contentWidth: width

        CoreText {
            id: title
            anchors.left: contentRow.left
            anchors.top: parent.top
            anchors.topMargin: 20
            text: qsTr("Request a payment")
            font.pixelSize: 21
            bold: true
        }

        RowLayout {
            id: contentRow

            enabled: walletController.initialized

            anchors.top: title.bottom
            anchors.topMargin: 40
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 30
            ColumnLayout {
                id: columnLayout
                Layout.minimumWidth: 450
                Layout.maximumWidth: 470

                spacing: 5

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
                        placeholderTextColor: enabled ? Theme.color.neutral7 : Theme.color.neutral4
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
                            color: enabled ? Theme.color.neutral7 : Theme.color.neutral4
                        }
                        Icon {
                            id: flipIcon
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            source: "image://images/flip-vertical"
                            color: unitLabel.enabled ? Theme.color.neutral8 : Theme.color.neutral4
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
                    labelText: qsTr("Label")
                    placeholderText: qsTr("Enter label...")
                }

                Separator {
                    Layout.fillWidth: true
                }

                LabeledTextInput {
                    id: message
                    Layout.fillWidth: true
                    labelText: qsTr("Message")
                    placeholderText: qsTr("Enter message...")
                }

                Separator {
                    Layout.fillWidth: true
                }

                Item {
                    Layout.fillWidth: true
                    Layout.minimumHeight: addressLabel.height + copyLabel.height
                    Layout.topMargin: 10
                    height: addressLabel.height + copyLabel.height
                    CoreText {
                        id: addressLabel
                        anchors.left: parent.left
                        anchors.top: parent.top
                        horizontalAlignment: Text.AlignLeft
                        width: 110
                        text: qsTr("Address")
                        font.pixelSize: 18
                    }
                    CoreText {
                        id: copyLabel
                        anchors.left: parent.left
                        anchors.top: addressLabel.bottom
                        horizontalAlignment: Text.AlignLeft
                        width: 110
                        text: qsTr("copy")
                        font.pixelSize: 18
                        color: enabled ? Theme.color.neutral7 : Theme.color.neutral4
                    }

                    Rectangle {
                        anchors.left: addressLabel.right
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        color: Theme.color.neutral2
                        radius: 5
                        CoreText {
                            id: address
                            anchors.fill: parent
                            anchors.leftMargin: 5
                            horizontalAlignment: Text.AlignLeft
                            font.pixelSize: 18
                            wrap: true
                        }
                    }
                }

                ContinueButton {
                    id: continueButton
                    Layout.fillWidth: true
                    Layout.topMargin: 30
                    text: qsTr("Create bitcoin address")
                    onClicked: {
                        if (!clearRequest.visible) {
                            requestCounter = requestCounter + 1
                            clearRequest.visible = true
                            title.text = qsTr("Payment request #" + requestCounter)
                            address.text = "bc1q f5xe y2tf 89k9 zy6k gnru wszy 5fsa truy 9te1 bu"
                            qrImage.code = "bc1qf5xey2tf89k9zy6kgnruwszy5fsatruy9te1bu"
                            continueButton.text = qsTr("Copy payment request")
                        }
                    }
                }

                ContinueButton {
                    id: clearRequest
                    Layout.fillWidth: true
                    Layout.topMargin: 10
                    visible: false
                    borderColor: Theme.color.neutral6
                    borderHoverColor: Theme.color.orangeLight1
                    borderPressedColor: Theme.color.orangeLight2
                    backgroundColor: "transparent"
                    backgroundHoverColor: "transparent"
                    backgroundPressedColor: "transparent"
                    text: qsTr("Clear")
                    onClicked: {
                        clearRequest.visible = false
                        title.text = qsTr("Request a payment")
                        address.text = ""
                        qrImage.code = ""
                        continueButton.text = qsTr("Create bitcoin address")
                    }
                }
            }

            Pane {
                Layout.alignment: Qt.AlignTop
                Layout.minimumWidth: 150
                Layout.minimumHeight: 150
                padding: 0
                background: Rectangle {
                    color: Theme.color.neutral2
                    visible: qrImage.code === ""
                }
                contentItem: QRImage {
                    id: qrImage
                    backgroundColor: "transparent"
                    foregroundColor: Theme.color.neutral9
                }
            }
        }
    }
}

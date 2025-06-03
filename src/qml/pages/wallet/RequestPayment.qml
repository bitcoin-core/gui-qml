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
    property WalletQmlModel wallet: walletController.selectedWallet
    property PaymentRequest request: wallet.currentPaymentRequest

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

                BitcoinAmountInputField {
                    Layout.fillWidth: true
                    enabled: walletController.initialized
                    amount: root.request.amount
                    errorText: root.request.amountError
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
                    Layout.minimumHeight: addressLabel.height + copyLabel.height + 20
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

                    CoreText {
                        id: address
                        anchors.left: addressLabel.right
                        anchors.right: parent.right
                        anchors.top: parent.top
                        text: root.request.address
                        horizontalAlignment: Text.AlignLeft
                        font.pixelSize: 18
                        wrapMode: Text.WrapAnywhere
                        background: Rectangle {
                            color: Theme.color.neutral2
                            radius: 5
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
                            wallet.commitPaymentRequest()
                            title.text = qsTr("Payment request #" + requestCounter)
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
                        root.request.clear()
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
                    code: root.request.address
                }
            }
        }
    }
}

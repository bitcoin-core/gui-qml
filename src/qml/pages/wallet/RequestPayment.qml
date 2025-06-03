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

    property WalletQmlModel wallet: walletController.selectedWallet
    property PaymentRequest request: wallet.currentPaymentRequest

    ScrollView {
        clip: true
        anchors.fill: parent
        contentWidth: width

        ColumnLayout {
            id: scrollContent
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: 20
            spacing: 20

            CoreText {
                id: title
                Layout.alignment: Qt.AlignLeft
                text: root.request.id === ""
                    ? qsTr("Request a payment")
                    : qsTr("Payment request #") + root.request.id
                font.pixelSize: 21
                bold: true
            }

            RowLayout {
                id: contentRow
                Layout.fillWidth: true
                enabled: walletController.initialized
                spacing: 30

                ColumnLayout {
                    id: formColumn
                    Layout.minimumWidth: 450
                    Layout.maximumWidth: 450

                    spacing: 10

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

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: 10

                        spacing: 0

                        ColumnLayout {
                            spacing: 5
                            Layout.alignment: Qt.AlignLeft
                            Layout.minimumWidth: 110

                            CoreText {
                                id: addressLabel
                                text: qsTr("Address")
                                font.pixelSize: 18
                            }
                            CoreText {
                                id: copyLabel
                                text: qsTr("copy")
                                font.pixelSize: 18
                                color: enabled ? Theme.color.neutral7 : Theme.color.neutral4
                            }
                        }

                        CoreText {
                            id: address
                            Layout.fillWidth: true
                            Layout.minimumHeight: 50
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
                                clearRequest.visible = true
                                root.wallet.commitPaymentRequest()
                                title.text = qsTr("Payment request #" + root.wallet.request.id)
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
}

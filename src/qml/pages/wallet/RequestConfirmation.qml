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
    property string label: "alice"
    property string message: "payment for goods"
    property string amount: "0.000"

    header: NavigationBar2 {
        id: navbar
        leftItem: NavButton {
            iconSource: "image://images/caret-left"
            text: qsTr("Back")
            onClicked: {
                root.StackView.view.pop()
            }
        }
        centerItem: Item {
            id: header
            Layout.fillWidth: true

            CoreText {
                anchors.left: parent.left
                text: qsTr("Payment request")
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

            Image {
                width: 60
                height: 60
                Layout.alignment: Qt.AlignHCenter
                source: "image://images/pending"
                sourceSize.width: 60
                sourceSize.height: 60
            }

            CoreText {
                Layout.alignment: Qt.AlignHCenter
                text: qsTr("Created just now")
                color: Theme.color.neutral7
                font.pixelSize: 18
            }

            LabeledTextInput {
                id: labelInput
                Layout.fillWidth: true
                labelText: qsTr("Label")
                visible: label != ""
                enabled: false
                text: label
            }

            Item {
                BitcoinAmount {
                    id: bitcoinAmount
                }

                height: 50
                Layout.fillWidth: true
                visible: amount != ""
                CoreText {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    color: Theme.color.neutral7
                    text: qsTr("Amount")
                    font.pixelSize: 15
                }

                TextField {
                    id: bitcoinAmountText
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
                    text: request.amount
                    enabled: false
                    selectByMouse: true
                    onTextChanged: {
                        bitcoinAmountText.text = bitcoinAmount.sanitize(bitcoinAmountText.text)
                    }
                }
            }


            LabeledTextInput {
                id: messageInput
                Layout.fillWidth: true
                labelText: qsTr("Message")
                visible: message != ""
                enabled: false
                text: message
            }

            Item {
                height: addressLabel.height + addressText.height
                Layout.fillWidth: true
                CoreText {
                    id: addressLabel
                    anchors.left: parent.left
                    anchors.top: parent.top
                    color: Theme.color.neutral7
                    text: qsTr("Address")
                    font.pixelSize: 15
                }

                CoreText {
                    id: addressText
                    anchors.left: parent.left
                    anchors.right: copyIcon.left
                    anchors.top: addressLabel.bottom
                    leftPadding: 0
                    font.family: "Inter"
                    font.styleName: "Regular"
                    font.pixelSize: 18
                    horizontalAlignment: Text.AlignLeft
                    color: Theme.color.neutral9
                    text: "bc1q wvlv mha3 cvhy q6qz tjzu mq2d 63ff htzy xxu6 q8"
                }

                Icon {
                    id: copyIcon
                    anchors.right: parent.right
                    anchors.verticalCenter: addressText.verticalCenter
                    source: "image://images/copy"
                    color: Theme.color.neutral8
                    size: 30
                    enabled: true
                    onClicked: {
                        Clipboard.setText(addressText.text)
                    }
                }
            }
        }
    }
}

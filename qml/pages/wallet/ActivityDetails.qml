// Copyright (c) 2024-2025 The Bitcoin Core developers
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
    signal showTransaction(string txid)

    property string txid: ""
    property bool canBump: false
    property string replacedByTxid: ""
    property string message: ""
    property string amount: ""
    property string label: ""
    property string address: ""
    property string direction: ""
    property string date: ""
    property int depth: 0
    property int type: 0
    property int status: 0

    property color iconColor: {
        if (delegate.status == Transaction.Confirmed) {
            if (delegate.type == Transaction.RecvWithAddress ||
                delegate.type == Transaction.RecvFromOther ||
                delegate.type == Transaction.Generated) {
                Theme.color.green
            } else {
                Theme.color.orange
            }
        } else {
            Theme.color.blue
        }
    }
    property color amountColor: {
        if (delegate.type == Transaction.RecvWithAddress
            || delegate.type == Transaction.RecvFromOther
            || delegate.type == Transaction.Generated) {
            Theme.color.green
        } else {
            Theme.color.neutral9
        }
    }

    id: root
    background: null

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
                text: qsTr("Transaction")
                font.pixelSize: 18
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
            opacity: root.replacedByTxid !== "" ? 0.4 : 1.0
            spacing: 0

            Rectangle {
                Layout.topMargin: 25
                Layout.bottomMargin: 25
                width: 60
                height: 60
                Layout.alignment: Qt.AlignHCenter
                radius: 30
                color: root.iconColor

                Icon {
                    anchors.centerIn: parent
                    source: {
                        if (root.type == Transaction.RecvWithAddress
                            || root.type == Transaction.RecvFromOther) {
                            "qrc:/icons/triangle-down"
                        } else if (root.type == Transaction.Generated) {
                            "qrc:/icons/coinbase"
                        } else {
                            "qrc:/icons/triangle-up"
                        }
                    }
                    color: Theme.color.white
                    size: 30
                }
            }

            CoreText {
                Layout.alignment: Qt.AlignHCenter
                Layout.bottomMargin: 5
                text: root.amount
                color: amountColor
                font.pixelSize: 28
            }

            CoreText {
                Layout.alignment: Qt.AlignHCenter
                text: root.date
                color: Theme.color.neutral7
                font.pixelSize: 18
            }

            CoreText {
                Layout.alignment: Qt.AlignHCenter
                Layout.bottomMargin: 10
                text: qsTr("%1 confirmations").arg(root.depth)
                color: Theme.color.neutral7
                font.pixelSize: 18
            }

            LabeledTextInput {
                id: labelTextInput
                Layout.fillWidth: true
                Layout.bottomMargin: 20
                labelText: qsTr("Label")
                visible: root.label != ""
                enabled: false
                text: root.label
            }

            LabeledTextInput {
                id: messageTextInput
                Layout.fillWidth: true
                Layout.bottomMargin: 20
                labelText: qsTr("Message")
                visible: root.message != ""
                enabled: false
                text: root.message
            }

            Item {
                height: addressLabel.height + addressText.height
                Layout.fillWidth: true
                Layout.bottomMargin: 20
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
                    font.family: "Roboto Mono"
                    font.styleName: "Regular"
                    font.pixelSize: 18
                    horizontalAlignment: Text.AlignLeft
                    color: Theme.color.neutral9
                    text: root.address
                    wrapMode: Text.WrapAnywhere
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
                        Clipboard.setText(addressText.text.replace(/\s+/g, ""))
                    }
                }
            }

            InfoBanner {
                objectName: "speedUpBanner"
                visible: root.canBump
                Layout.alignment: Qt.AlignHCenter
                Layout.fillWidth: true
                Layout.maximumWidth: 600
                Layout.topMargin: 20
                bannerLayout: InfoBanner.Layout.Vertical
                message: qsTr("This transaction is still unconfirmed. You can speed it up by increasing the fee.")
                primaryButtonText: qsTr("Speed up")
                onPrimaryClicked: speedUpOverlay.open()
            }

        }
    }

    InfoBanner {
        objectName: "replacedBanner"
        visible: root.replacedByTxid !== ""
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 20
        anchors.bottomMargin: 30
        title: qsTr("This transaction was updated with a faster one")
        message: qsTr("You increased the fee while this transaction was still unconfirmed. Only the new one will confirm on-chain.")
        primaryButtonText: qsTr("View updated transaction")
        onPrimaryClicked: root.showTransaction(root.replacedByTxid)
    }

    SpeedUpOverlay {
        id: speedUpOverlay
        txid: root.txid
        anchors.centerIn: parent
        onBumpSucceeded: {
            var page = root.StackView.view.push("SendResult.qml", {
                resultType: SendResult.ResultType.SpeedUp
            })
            page.done.connect(function() {
                if (walletController.selectedWallet) {
                    walletController.selectedWallet.activityListModel.reload()
                }
                root.StackView.view.pop(null)
            })
            page.viewNewTransaction.connect(function() {
                if (walletController.selectedWallet) {
                    walletController.selectedWallet.activityListModel.reload()
                }
                root.StackView.view.pop(null)
                root.showTransaction(speedUpOverlay.newTxid)
            })
        }
    }
}

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
    property WalletQmlModelTransaction transaction: walletController.selectedWallet.currentTransaction

    signal finished()
    signal back()
    signal transactionSent()

    header: NavigationBar2 {
        id: navbar
        leftItem: NavButton {
            iconSource: "image://images/caret-left"
            text: qsTr("Back")
            onClicked: {
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

            CoreText {
                id: title
                Layout.topMargin: 30
                Layout.bottomMargin: 15
                text: qsTr("Transaction details")
                font.pixelSize: 21
                bold: true
            }

            ListView {
                id: inputsList
                Layout.fillWidth: true
                Layout.preferredHeight: contentHeight
                model: root.wallet.recipients
                delegate: Item {
                    id: delegate
                    height: 55
                    width: ListView.view.width

                    required property string address;
                    required property string label;
                    required property string amount;

                    RowLayout {
                        spacing: 10
                        anchors.fill: parent
                        CoreText {
                            Layout.alignment: Qt.AlignVCenter | Qt.AlignLeft
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignLeft
                            text: label == "" ? address : label
                            font.pixelSize: 18
                            elide: Text.ElideMiddle
                        }

                        CoreText {
                            Layout.alignment: Qt.AlignVCenter | Qt.AlignRight
                            text: amount
                            font.pixelSize: 18
                        }
                    }

                    Separator {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        color: Theme.color.neutral3
                    }
                }
            }

            RowLayout {
                Layout.topMargin: 20
                CoreText {
                    text: qsTr("Total amount")
                    font.pixelSize: 20
                    color: Theme.color.neutral9
                    horizontalAlignment: Text.AlignLeft
                }
                Item {
                    Layout.fillWidth: true
                }
                CoreText {
                    text: root.transaction.total
                    font.pixelSize: 20
                    color: Theme.color.neutral9
                }
            }

            Separator {
                Layout.fillWidth: true
                color: Theme.color.neutral3
            }

            RowLayout {
                CoreText {
                    text: qsTr("Fee")
                    font.pixelSize: 18
                    Layout.preferredWidth: 110
                    horizontalAlignment: Text.AlignLeft
                }
                Item {
                    Layout.fillWidth: true
                }
                CoreText {
                    text: root.transaction.fee
                    font.pixelSize: 15
                }
            }

            Separator {
                Layout.fillWidth: true
                color: Theme.color.neutral3
            }

            ContinueButton {
                id: confimationButton
                Layout.fillWidth: true
                Layout.topMargin: 30
                text: qsTr("Send")
                onClicked: {
                    root.wallet.sendTransaction()
                    root.transactionSent()
                }
            }
        }
    }
}

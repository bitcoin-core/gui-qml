// Copyright (c) 2024-2025 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

import "../../controls"
import "../../components"

PageStack {
    id: stackView

    function navigateToTransaction(txid) {
        if (!walletController.selectedWallet)
            return

        var details = walletController.selectedWallet.activityListModel.transactionDetails(txid)
        if (Object.keys(details).length === 0)
            return

        var page = stackView.push("ActivityDetails.qml", details)
        page.showTransaction.connect(stackView.navigateToTransaction)
    }

    Connections {
        target: walletController
        function onSelectedWalletChanged() {
            stackView.pop()
        }
    }

    Binding {
        target: walletController.selectedWallet
        property: "displayUnit"
        value: optionsModel.displayUnit
        when: walletController.selectedWallet !== null
    }

    initialItem: RowLayout {
        Page {
            Layout.alignment: Qt.AlignCenter
            Layout.fillHeight: true
            Layout.fillWidth: true
            Layout.maximumWidth: 600
            Layout.margins: 20
            id: root
            background: null

            header: CoreText {
                id: title
                objectName: "walletActivityTitle"
                horizontalAlignment: Text.AlignLeft
                text: qsTr("Activity")
                font.pixelSize: 21
                bold: true
            }

            contentItem: Item {
                Loader {
                    id: skeletonOverlay

                    width: Math.min(parent.width - 40, 600)


                    active: !walletController.initialized

                    sourceComponent: Column {
                        spacing: 0
                        Repeater {
                            model: 5
                            delegate: ItemDelegate {
                                height: 51
                                width: skeletonOverlay.width
                                contentItem: RowLayout {
                                    spacing: 12
                                    Skeleton {
                                        Layout.leftMargin: 6
                                        width: 15
                                        height: 15
                                    }
                                    Skeleton {
                                        height: 15
                                        Layout.fillWidth: true
                                    }
                                    Skeleton {
                                        width: 75
                                        height: 15
                                    }
                                    Skeleton {
                                        width: 120
                                        height: 15
                                    }
                                }
                                background: Item {
                                    Separator {
                                        anchors.bottom: parent.bottom
                                        width: parent.width
                                    }
                                }
                            }
                        }
                    }
                }

                ListView {
                    id: listView
                    anchors.fill: parent
                    clip: true
                    model: walletController.selectedWallet.activityListModel
                    delegate: ItemDelegate {
                        id: delegate
                        objectName: "activityItem_" + delegate.txid
                        required property string address;
                        required property string amount;
                        required property string date;
                        required property int depth;
                        required property string label;
                        required property int status;
                        required property int type;
                        required property string txid;
                        required property bool canBump;
                        required property string replacedByTxid;

                        HoverHandler {
                            cursorShape: Qt.PointingHandCursor
                        }

                        opacity: (delegate.replacedByTxid !== "" || delegate.status === Transaction.Conflicted) ? 0.4 : 1.0

                        onClicked: {
                            var page = stackView.push(detailsPage)
                            page.showTransaction.connect(stackView.navigateToTransaction)
                        }

                        width: ListView.view.width

                        background: Item {
                            Separator {
                                anchors.bottom: parent.bottom
                                width: parent.width
                            }
                        }

                        contentItem: RowLayout {
                            Icon {
                                Layout.alignment: Qt.AlignCenter
                                Layout.margins: 6
                                source: {
                                    if (delegate.type == Transaction.RecvWithAddress
                                        || delegate.type == Transaction.RecvFromOther) {
                                        "qrc:/icons/triangle-down"
                                    } else if (delegate.type == Transaction.Generated) {
                                        "qrc:/icons/coinbase"
                                    } else {
                                        "qrc:/icons/triangle-up"
                                    }
                                }
                                color: {
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
                                size: 14
                            }
                            CoreText {
                                Layout.alignment: Qt.AlignCenter
                                Layout.fillWidth: true
                                Layout.preferredWidth: 0
                                Layout.margins: 6
                                wrap: false
                                color: delegate.hovered ? Theme.color.orange : Theme.color.neutral9
                                elide: Text.ElideMiddle
                                text: delegate.label !== "" ? delegate.label : delegate.address
                                font.pixelSize: 15
                                horizontalAlignment: Text.AlignLeft
                                clip: true
                            }

                            CoreText {
                                Layout.alignment: Qt.AlignCenter
                                Layout.preferredWidth: 110
                                Layout.margins: 6
                                wrap: false
                                text: delegate.date
                                font.pixelSize: 15
                                horizontalAlignment: Text.AlignRight
                            }

                            CoreText {
                                Layout.alignment: Qt.AlignCenter
                                Layout.preferredWidth: 140
                                Layout.margins: 6
                                wrap: false
                                text: delegate.amount
                                font.pixelSize: 15
                                horizontalAlignment: Text.AlignRight
                                color: {
                                    if (delegate.type == Transaction.RecvWithAddress
                                        || delegate.type == Transaction.RecvFromOther
                                        || delegate.type == Transaction.Generated) {
                                        Theme.color.green
                                    } else {
                                        Theme.color.neutral9
                                    }
                                }
                            }

                            Component {
                                id: detailsPage
                                ActivityDetails {
                                    txid: delegate.txid
                                    canBump: delegate.canBump
                                    replacedByTxid: delegate.replacedByTxid
                                    amount: delegate.amount
                                    date: delegate.date
                                    depth: delegate.depth
                                    type: delegate.type
                                    status: delegate.status
                                    address: delegate.address
                                    label: delegate.label
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

// Copyright (c) 2025 The Bitcoin Core developers
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

    property WalletQmlModel wallet: walletController.selectedWallet

    signal done()

    header: NavigationBar2 {
        centerItem: Header {
            headerBold: true
            headerSize: 18
            header: qsTr("Coin Selection")
        }
        rightItem: NavButton {
            text: qsTr("Done")
            onClicked: root.done()
        }
    }

    background: Rectangle {
        color: Theme.color.neutral0
    }

    ListView {
        id: listView
        clip: true
        width: Math.min(parent.width - 40, 450)
        height: parent.height
        anchors.horizontalCenter: parent.horizontalCenter
        model: root.wallet.coinsListModel
        spacing: 15

        header: ColumnLayout {
            width: listView.width
            RowLayout {
                Layout.fillWidth: true
                spacing: 15
                CoreText {
                    Layout.alignment: Qt.AlignLeft
                    Layout.fillWidth: true
                    Layout.preferredWidth: 0
                    font.pixelSize: 18
                    color: Theme.color.neutral9
                    elide: Text.ElideMiddle
                    wrapMode: Text.NoWrap
                    horizontalAlignment: Text.AlignLeft
                    text: qsTr("Total selected")
                }
                CoreText {
                    Layout.alignment: Qt.AlignRight
                    color: Theme.color.neutral9
                    font.pixelSize: 18
                    text: root.wallet.coinsListModel.totalSelected
                }
            }
            RowLayout {
                Layout.bottomMargin: 30
                CoreText {
                    Layout.alignment: Qt.AlignLeft
                    Layout.fillWidth: true
                    Layout.preferredWidth: 0
                    font.pixelSize: 15
                    color: Theme.color.neutral7
                    elide: Text.ElideMiddle
                    wrapMode: Text.NoWrap
                    horizontalAlignment: Text.AlignLeft
                    text: if (root.wallet.coinsListModel.overRequiredAmount) {
                        qsTr("Over required amount")
                    } else {
                        qsTr("Remaining to select")
                    }
                }
                CoreText {
                    Layout.alignment: Qt.AlignRight
                    font.pixelSize: 15
                    color: Theme.color.neutral7
                    text: root.wallet.coinsListModel.changeAmount
                }
            }
        }

        delegate: ItemDelegate {
            id: delegate
            required property string address;
            required property string amount;
            required property string label;
            required property bool locked;
            required property bool selected;

            required property int index;

            readonly property color stateColor: {
                if (delegate.down) {
                    return Theme.color.orange
                } else if (delegate.hovered) {
                    return Theme.color.orangeLight1
                }
                return Theme.color.neutral9
            }

            leftPadding: 0
            rightPadding: 0
            topPadding: 14
            bottomPadding: 14
            width: listView.width

            background: Item {
                Separator {
                    anchors.top: parent.top
                    width: parent.width
                }
            }

            contentItem: RowLayout {
                width: parent.width
                CoreCheckBox {
                    id: checkBox
                    Layout.minimumWidth: 20
                    enabled: !locked
                    checked: selected
                    visible: !locked
                    MouseArea {
                        anchors.fill: parent
                        enabled: false
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                    }

                    onToggled: listView.model.toggleCoinSelection(index)
                }
                Icon {
                    source: "qrc:/icons/lock"
                    color: Theme.color.neutral9
                    visible: locked
                    size: 20
                }
                CoreText {
                    text: amount
                    font.pixelSize: 18
                }
                CoreText {
                    Layout.fillWidth: true
                    text: label != "" ? label : address
                    font.pixelSize: 18
                    elide: Text.ElideMiddle
                    wrapMode: Text.NoWrap
                }
            }
        }
    }
}
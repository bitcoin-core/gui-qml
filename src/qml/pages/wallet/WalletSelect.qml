// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import "../../controls"

Popup {
    id: root

    property alias model: listView.model
    implicitHeight: listView.height + arrow.height
    implicitWidth: 250

    background: Item {
        anchors.fill: parent
        Rectangle {
            id: tooltipBg
            color: Theme.color.neutral0
            border.color: Theme.color.neutral4
            radius: 5
            border.width: 1
            width: parent.width
            height: parent.height - arrow.height - 1
            anchors.top: arrow.bottom
            anchors.horizontalCenter: root.horizontalCenter
            anchors.topMargin: -1
        }
        Image {
            id: arrow
            source: Theme.image.tooltipArrow
            width: 22
            height: 10
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
        }
    }

    ButtonGroup {
        id: buttonGroup
    }

    ListView {
        id: listView
        anchors.topMargin: arrow.height
        width: 220
        height: contentHeight
        interactive: false
        spacing: 2
        model: walletListModel

        header: CoreText {
            text: qsTr("Wallets")
            visible: listView.count > 0
            bold: true
            width: 220
            height: 30
            color: Theme.color.neutral9
            font.pixelSize: 14
            topPadding: 10
            bottomPadding: 5
        }

        delegate: WalletBadge {
            required property string name;

            width: 220
            height: 32
            text: name
            ButtonGroup.group: buttonGroup
            showBalance: false
            showIcon: false
            onClicked: {
                walletListModel.selectedWallet = name
                root.close()
            }

        }

        footer: RowLayout {
            id: addWallet
            height: 45
            width: addIcon.size + addText.width + spacing
            anchors.horizontalCenter: parent.horizontalCenter
            Icon {
                id: addIcon
                Layout.alignment: Qt.AlignHCenter
                source: "image://images/plus"
                color: Theme.color.neutral8
                size: 14
                topPadding: 5
                bottomPadding: 10
            }
            CoreText {
                id: addText
                Layout.alignment: Qt.AlignHCenter
                text: qsTr("Add Wallet")
                color: Theme.color.neutral9
                font.pixelSize: 15
                topPadding: 5
                bottomPadding: 10
            }
        }
    }
}

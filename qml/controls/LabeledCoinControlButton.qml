// Copyright (c) 2025 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15

Item {
    property int coinsSelected: 0
    property int coinCount: 0
    property string valueObjectName: ""

    signal openCoinControl

    id: root
    implicitHeight: 56

    function click() {
        if (coinCount > 0) {
            root.openCoinControl()
        }
    }

    CoreText {
        id: label
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        horizontalAlignment: Text.AlignLeft
        width: 128
        font: Theme.text.body.font
        lineHeight: Theme.text.body.lineHeight
        lineHeightMode: Text.FixedHeight
        text: qsTr("Coins")
    }

    CoreText {
        objectName: root.valueObjectName
        anchors.left: label.right
        anchors.verticalCenter: parent.verticalCenter
        horizontalAlignment: Text.AlignLeft
        color: enabled ? Theme.color.orangeLight1 : Theme.color.neutral2
        font: Theme.text.body.font
        lineHeight: Theme.text.body.lineHeight
        lineHeightMode: Text.FixedHeight
        text: {
            if (root.coinCount === 0) {
                qsTr("No coins available")
            } else if (root.coinsSelected === 0) {
                qsTr("Select")
            } else {
                qsTr("%n coin selected", "", root.coinsSelected)
            }
        }

        MouseArea {
            anchors.fill: parent
            onClicked: root.click()
            cursorShape: Qt.PointingHandCursor
        }
    }
}

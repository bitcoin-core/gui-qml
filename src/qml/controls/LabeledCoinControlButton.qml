// Copyright (c) 2025 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Item {
    property int coinsSelected: 0

    signal openCoinControl

    id: root
    implicitHeight: label.height

    CoreText {
        id: label
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        horizontalAlignment: Text.AlignLeft
        width: 110
        color: Theme.color.neutral9
        font.pixelSize: 18
        text: qsTr("Inputs")
    }

    CoreText {
        anchors.left: label.right
        anchors.verticalCenter: parent.verticalCenter
        horizontalAlignment: Text.AlignLeft
        color: Theme.color.orangeLight1
        font.pixelSize: 18
        text: {
            if (coinsSelected === 0) {
                qsTr("Select")
            } else {
                qsTr("%1 input%2 selected")
                    .arg(coinsSelected)
                    .arg(coinsSelected === 1 ? "" : "s")
            }
        }

        MouseArea {
            anchors.fill: parent
            onClicked: root.openCoinControl()
            cursorShape: Qt.PointingHandCursor
        }
    }
}

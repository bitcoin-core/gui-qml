// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15

import "../controls"

NeutralButton {
    id: root

    buttonSize: NeutralButton.Medium
    leftPadding: Theme.space.xxs
    text: qsTr("Back")
    textStyle: Theme.text.caption

    contentItem: Item {
        implicitWidth: backChevron.width - 3 + backLabel.implicitWidth
        implicitHeight: Math.max(backChevron.height, backLabel.implicitHeight)

        Icon {
            id: backChevron
            source: "qrc:/icons/caret-left.svg"
            size: 18
            color: root.textColor
            anchors.verticalCenter: parent.verticalCenter
        }
        CoreText {
            id: backLabel
            x: backChevron.width - 3
            anchors.verticalCenter: parent.verticalCenter
            text: root.text
            textStyle: root.textStyle
            color: root.textColor
            wrap: false
        }
    }
}

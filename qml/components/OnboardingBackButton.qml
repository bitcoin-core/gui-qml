// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15

import "../controls"

NeutralButton {
    id: root

    buttonSize: NeutralButton.Medium
    leftPadding: mirrored ? horizontalPadding : Theme.space.xxs
    rightPadding: mirrored ? Theme.space.xxs : horizontalPadding
    text: qsTr("Back")
    textStyle: Theme.text.caption

    contentItem: Row {
        spacing: -3

        Icon {
            id: backChevron
            source: "qrc:/icons/caret-left.svg"
            size: 18
            color: root.textColor
            anchors.verticalCenter: parent.verticalCenter
        }
        CoreText {
            id: backLabel
            anchors.verticalCenter: parent.verticalCenter
            text: root.text
            textStyle: root.textStyle
            color: root.textColor
            wrap: false
        }
    }
}

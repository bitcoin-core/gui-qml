// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15

NeutralButton {
    id: root

    buttonSize: NeutralButton.Medium
    leftPadding: 2
    text: qsTr("Back")

    contentItem: Item {
        implicitWidth: backChevron.width - 3 + backLabel.implicitWidth
        implicitHeight: Math.max(backChevron.height, backLabel.implicitHeight)

        Icon {
            id: backChevron
            source: "image://images/caret-left"
            size: 18
            color: root.textColor
            anchors.verticalCenter: parent.verticalCenter
        }
        CoreText {
            id: backLabel
            x: backChevron.width - 3
            anchors.verticalCenter: parent.verticalCenter
            text: root.text
            font: Theme.text.captionStrong.font
            color: root.textColor
            wrap: false
        }
    }
}

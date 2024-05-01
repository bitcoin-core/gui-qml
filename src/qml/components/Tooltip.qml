// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15

import "../controls"

Item {
    id: root

    property alias text: tooltipText.text

    Rectangle {
        id: tooltipBg
        color: Theme.color.neutral0
        border.color: Theme.color.neutral4
        radius: 5
        border.width: 1
        width: tooltipText.width + 30
        height: tooltipText.height + 20
        anchors.top: arrow.bottom
        anchors.right: arrow.right
        anchors.rightMargin: -10
        anchors.topMargin: -1
    }

    Image {
        id: arrow
        source: Theme.image.tooltipArrow
        width: 22
        height: 10
        anchors.horizontalCenter: root.horizontalCenter
        anchors.top: root.top
    }

    CoreText {
        id: tooltipText
        text: ""
        wrapMode: Text.NoWrap
        anchors.centerIn: tooltipBg
    }
}

// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick
import QtQuick.Controls

Button {
    font.family: "Inter"
    font.styleName: "Semi Bold"
    font.pointSize: 18
    contentItem: Text {
        text: parent.text
        font: parent.font
        color: "white"
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
    background: Rectangle {
        implicitHeight: 46
        implicitWidth: 300
        color: "#F7931A"
        radius: 5
    }
}

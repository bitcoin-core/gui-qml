// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick
import QtQuick.Controls

Button {
    font.family: "Inter"
    font.styleName: "Semi Bold"
    font.pixelSize: 18
    contentItem: Text {
        text: parent.text
        font: parent.font
        color: Theme.color.neutral9
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
    background: Rectangle {
        implicitHeight: 46
        implicitWidth: 340
        color: Theme.color.background
        radius: 5
        border {
            width: 1
            color: Theme.color.neutral4
        }
    }
}

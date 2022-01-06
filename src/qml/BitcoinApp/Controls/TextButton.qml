// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick
import QtQuick.Controls

Button {
    id: root
    property int textSize: 18
    property string textColor: Theme.color.neutral9
    font.family: "Inter"
    font.styleName: "Semi Bold"
    font.pointSize: root.textSize
    contentItem: Text {
        text: root.text
        font: root.font
        color: root.textColor
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
    background: null
}

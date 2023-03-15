// Copyright (c) 2023 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15

Text {
    property bool bold: false
    property bool wrap: true
    color: Theme.color.white
    font.family: "Inter"
    font.styleName: bold ? "Semi Bold" : "Regular"
    font.pixelSize: 13
    horizontalAlignment: Text.AlignHCenter
    verticalAlignment: Text.AlignVCenter
    wrapMode: wrap ? Text.WordWrap : Text.NoWrap

    Behavior on color {
        ColorAnimation { duration: 150 }
    }
}

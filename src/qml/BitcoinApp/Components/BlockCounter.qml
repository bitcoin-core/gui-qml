// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// The BlockCounter component.

import QtQuick
import QtQuick.Controls

Label {
    property int blockHeight: 0
    background: Rectangle {
        color: "black"
    }
    color: "orange"
    padding: 16
    horizontalAlignment: Text.AlignHCenter
    verticalAlignment: Text.AlignVCenter
    font.pixelSize: height / 3
    text: blockHeight
}

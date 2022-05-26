// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// The BlockCounter component.

import QtQuick 2.15
import QtQuick.Controls 2.15

Label {
    property int blockHeight: 0
    background: Rectangle {
        color: "black"
    }
    color: "orange"
    padding: 16
    horizontalAlignment: Text.AlignHCenter
    verticalAlignment: Text.AlignVCenter
    font.family: "Inter"
    font.styleName: "Semi Bold"
    font.pixelSize: 20
    text: blockHeight
}

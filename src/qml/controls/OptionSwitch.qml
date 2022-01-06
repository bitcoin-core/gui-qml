// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15

Switch {
    id: root
    indicator: Rectangle {
        implicitWidth: 45
        implicitHeight: 28
        x: root.leftPadding
        y: parent.height / 2 - height / 2
        radius: 18
        color: root.checked ? "#F7931A" : "#DDDDDD"
        Rectangle {
            id: indicatorButton
            y: parent.height / 2 - height / 2
            x: root.checked ? parent.width - width - 4 : 0 + 4
            width: 20
            height: 20
            radius: 18
            color: "#ffffff"
        }
    }
}

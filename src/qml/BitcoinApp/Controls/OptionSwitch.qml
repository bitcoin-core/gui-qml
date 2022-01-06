// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick
import QtQuick.Controls

Switch {
    id: root
    indicator: Rectangle {
        implicitWidth: 45
        implicitHeight: 28
        x: root.leftPadding
        y: Math.round((parent.height - height) / 2)
        radius: 18
        color: root.checked ? Theme.color.orange : Theme.color.neutral4
        Rectangle {
            id: indicatorButton
            y: parent.height / 2 - height / 2
            x: root.checked ? parent.width - width - 4 : 0 + 4
            width: 20
            height: 20
            radius: 18
            color: Theme.color.white
        }
    }
}

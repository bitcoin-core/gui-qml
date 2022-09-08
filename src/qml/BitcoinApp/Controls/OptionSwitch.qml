// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick
import QtQuick.Controls

Switch {
    id: root
    implicitWidth: 45
    implicitHeight: 28
    background: Rectangle {
        radius: Math.floor(height / 2)
        color: root.checked ? Theme.color.orange : Theme.color.neutral4
    }
    indicator: Rectangle {
        property real _margin: Math.round((parent.height - height) / 2)
        y: Math.round(parent.height - height) / 2
        x: root.checked ? parent.width - width - _margin : _margin
        radius: 10
        width: 20
        height: 20
        color: Theme.color.white
        Behavior on x {
            SmoothedAnimation {
            }
        }
    }
}

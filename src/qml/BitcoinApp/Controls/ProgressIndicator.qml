// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick
import QtQuick.Controls

Control {
    property real progress: 0
    Behavior on progress {
        NumberAnimation {
            easing.type: Easing.Bezier
            easing.bezierCurve: [0.5, 0.0, 0.2, 1, 1, 1]
            duration: 250
        }
    }
    contentItem: Rectangle {
        implicitHeight: 6
        radius: Math.floor(height / 2)
        color: "#404040"
        Item {
            width: Math.round(progress * contentItem.width)
            height: parent.height
            clip: true
            Rectangle {
                width: contentItem.width
                height: contentItem.height
                radius: contentItem.radius
                color: "orange"
            }
        }
    }
}

// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15

Control {
    id: root
    property real progress: 0
    Behavior on progress {
        NumberAnimation {
            easing.type: Easing.Bezier
            easing.bezierCurve: [0.5, 0.0, 0.2, 1, 1, 1]
            duration: 250
        }
    }
    contentItem: Rectangle {
        id: progressTrack
        implicitHeight: 6
        radius: Math.floor(height / 2)
        color: Theme.color.neutral3
        Item {
            width: Math.round(root.progress * root.contentItem.width)
            height: parent.height
            clip: true
            Rectangle {
                width: root.contentItem.width
                height: root.contentItem.height
                radius: progressTrack.radius
                color: Theme.color.orange
            }
        }
    }
}

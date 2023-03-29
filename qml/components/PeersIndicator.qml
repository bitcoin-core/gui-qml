// Copyright (c) 2023 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15
import "../controls"

RowLayout {
    id: root
    required property int numOutboundPeers
    required property int maxNumOutboundPeers
    required property bool paused
    property int size: 5

    spacing: 5
    Repeater {
        model: 5
        Rectangle {
            width: 3
            height: 3
            radius: width / 2
            color: Theme.color.neutral9
            opacity: (index === 0 && root.numOutboundPeers > 0) || (index + 1 <= root.size * root.numOutboundPeers / root.maxNumOutboundPeers) ? 0.95 : 0.45
            Behavior on opacity { OpacityAnimator { duration: 150 } }
            SequentialAnimation on opacity {
                loops: Animation.Infinite
                running: numOutboundPeers === 0 && index === 0 && !root.paused
                SmoothedAnimation { to: 0; velocity: 2.2 }
                SmoothedAnimation { to: 1; velocity: 2.2 }
            }
            PropertyAnimation on opacity {
                running: root.paused
                to: .45;
                duration: 150
            }

            Behavior on color {
                ColorAnimation { duration: 150 }
            }
        }
    }
}

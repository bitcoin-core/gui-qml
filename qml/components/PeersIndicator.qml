// Copyright (c) 2023 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import "../controls"

Row {
    id: root
    required property int numOutboundPeers
    required property int maxNumOutboundPeers
    required property bool paused
    property int size: 5
    property real indicatorDimensions: 3
    property real indicatorSpacing: 5

    height: root.indicatorDimensions

    spacing: root.indicatorSpacing
    Repeater {
        model: 5
        Rectangle {
            width: root.indicatorDimensions
            height: root.indicatorDimensions
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

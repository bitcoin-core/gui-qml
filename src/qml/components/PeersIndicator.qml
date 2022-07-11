// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// The PeersIndicator component.
// See reference design: https://bitcoindesign.github.io/Bitcoin-Core-App/assets/images/block-clock-connection-states-big.png

import QtQuick 2.15
import "../controls"

Item {
    id: root
    required property int numOutboundPeers
    required property int maxNumOutboundPeers

    implicitWidth: dots.childrenRect.width
    implicitHeight: dots.childrenRect.height

    Row {
        id: dots
        property int size: 5
        spacing: 5
        Repeater {
            model: 5
            Rectangle {
                width: 3
                height: 3
                radius: width / 2
                color: Theme.color.neutral9
                opacity: (index === 0 && root.numOutboundPeers > 0) || (index + 1 <= dots.size * root.numOutboundPeers / root.maxNumOutboundPeers) ? 0.95 : 0.45
                Behavior on opacity { OpacityAnimator { duration: 100 } }
                SequentialAnimation on opacity {
                    loops: Animation.Infinite
                    running: numOutboundPeers === 0 && index === 0
                    SmoothedAnimation { to: 0; velocity: 2.2 }
                    SmoothedAnimation { to: 1; velocity: 2.2 }
                }
            }
        }
    }
}

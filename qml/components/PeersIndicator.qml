// Copyright (c) 2023 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Window 2.15
import "../controls"

Row {
    id: root
    required property int numOutboundPeers
    required property int maxNumOutboundPeers
    required property bool paused
    required property bool active
    readonly property bool windowVisible: root.Window.window !== null &&
        root.Window.window.visible &&
        root.Window.window.visibility !== Window.Hidden &&
        root.Window.window.visibility !== Window.Minimized
    readonly property bool presentationActive: root.active && root.visible && root.windowVisible
    property int size: 5
    property real indicatorDimensions: 3
    property real indicatorSpacing: 5

    height: root.indicatorDimensions

    spacing: root.indicatorSpacing
    Repeater {
        model: 5
        Rectangle {
            id: dot
            objectName: "peerIndicatorDot" + index
            readonly property real restingOpacity: (index === 0 && root.numOutboundPeers > 0)
                || (index + 1 <= root.size * root.numOutboundPeers / root.maxNumOutboundPeers) ? 0.95 : 0.45
            property real pulseOpacity: restingOpacity
            width: root.indicatorDimensions
            height: root.indicatorDimensions
            radius: width / 2
            color: Theme.color.neutral9
            opacity: pulse.running ? pulseOpacity : root.paused ? 0.45 : restingOpacity
            Behavior on opacity { enabled: Theme.motion.animated && !pulse.running; OpacityAnimator { duration: Theme.motion.normal } }
            SequentialAnimation on pulseOpacity {
                id: pulse
                loops: Animation.Infinite
                running: root.presentationActive && numOutboundPeers === 0 && index === 0 && !root.paused
                    && Theme.motion.decorativeLoops
                SmoothedAnimation { to: 0; velocity: 2.2 }
                SmoothedAnimation { to: 1; velocity: 2.2 }
            }

            Behavior on color {
                enabled: Theme.motion.animated
                ColorAnimation { duration: Theme.motion.normal }
            }
        }
    }
}

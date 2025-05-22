// Copyright (c) 2025 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15

Rectangle {
    id: root
    property color baseColor: Theme.color.neutral1
    property color highlightColor: Theme.color.neutral2
    property int shimmerDuration: 2500
    property bool loading: true

    radius: 3

    gradient: Gradient {
        orientation: Gradient.Horizontal
        GradientStop {
            id: stop1
            position: 0.0
            color: root.baseColor
        }
        GradientStop {
            id: stop2
            position: 0.5
            color: root.highlightColor
        }
        GradientStop {
            id: stop3;
            position: 1
            color: root.baseColor
        }
    }

    ParallelAnimation  {
        running: loading
        loops: Animation.Infinite
        NumberAnimation {
            target: stop1
            property: "position"
            from: -1.0
            to: 1.0
            duration: root.shimmerDuration
            easing.type: Easing.Linear
        }
        NumberAnimation {
            target: stop2
            property: "position"
            from: -0.5
            to: 1.5
            duration: root.shimmerDuration;
            easing.type: Easing.Linear
        }
        NumberAnimation {
            target: stop3
            property: "position"
            from: 0.0
            to: 2.0
            duration: root.shimmerDuration
            easing.type: Easing.Linear
        }
    }
}

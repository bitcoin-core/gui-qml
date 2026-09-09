// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15

Loader {
    id: root

    property var target: parent
    property string text: ""
    property bool below: true
    property bool shown: !!target && target.hovered === true

    active: root.shown && root.text.length > 0
    z: 100

    anchors.horizontalCenter: target ? target.horizontalCenter : undefined
    anchors.top: root.below && target ? target.bottom : undefined
    anchors.bottom: !root.below && target ? target.top : undefined

    sourceComponent: Tooltip {
        text: root.text
        arrowAtBottom: !root.below
    }
}

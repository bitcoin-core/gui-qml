// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15

Item {
    property var timeRatioList: []
    property real verificationProgress: 0
    property bool connected: false
    property bool synced: false
    property bool paused: false
    property bool animateDial: true
    property int connectingAnimationDelayMs: 5000
    property bool showTimeTicks: true
    property bool showBlockSegments: true
    property bool useGradientArcWhenSynced: false
    property real penWidth: 4
    property color backgroundColor: "transparent"
    property var confirmationColors: []
    property color timeTickColor: "transparent"
}

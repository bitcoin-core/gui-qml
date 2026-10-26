// Copyright (c) 2023 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15

 Rectangle {
    id: root
    property int topMargin: -Theme.focusRing.offset
    property int bottomMargin: -Theme.focusRing.offset
    property int leftMargin: -Theme.focusRing.offset
    property int rightMargin: -Theme.focusRing.offset
    property int borderRadius: 9
    // Corner radius of the surrounded item; the ring then follows it at its offset.
    property real targetRadius: -1

    anchors.fill: parent
    anchors.topMargin: root.topMargin
    anchors.bottomMargin: root.bottomMargin
    anchors.leftMargin: root.leftMargin
    anchors.rightMargin: root.rightMargin
    border.width: Theme.focusRing.width
    border.color: Theme.color.focus
    radius: root.targetRadius >= 0 ? Math.max(0, root.targetRadius - root.leftMargin) : root.borderRadius
    color: "transparent"

    Behavior on border.color {
        enabled: Theme.motion.animated
        ColorAnimation { duration: Theme.motion.normal }
    }
}

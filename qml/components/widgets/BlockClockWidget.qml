// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import ".."
import "../../controls"

DashboardWidget {
    id: root
    CoreText {
        id: title
        objectName: "blockClockWidgetTitle"
        anchors.top: parent.top
        anchors.left: parent.left
        width: parent.width
        text: qsTr("Blockclock")
        font: root.scaledFont(Theme.text.subheading)
        color: Theme.color.neutral8
        horizontalAlignment: Text.AlignLeft
        elide: Text.ElideRight
        wrap: false
    }
    Item {
        anchors.top: title.bottom
        anchors.topMargin: 8
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        BlockClock {
            anchors.centerIn: parent
            parentWidth: parent.width
            parentHeight: parent.height
            fillAvailableSpace: true
            showNetworkIndicator: false
            renderingActive: root.active
        }
    }
}

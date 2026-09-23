// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import "../../controls"

Item {
    id: root
    property bool inactive: false
    property color surfaceColor: Theme.color.neutral1
    property string statusText: inactive ? qsTr("Associated payment request") : qsTr("Payment request fulfilled")
    implicitWidth: 18
    implicitHeight: 18
    Accessible.role: Accessible.StaticText
    Accessible.name: statusText

    Icon {
        anchors.centerIn: parent
        source: "qrc:/icons/activity-invoice.svg"
        color: Theme.color.neutral7
        size: 18
    }
    Rectangle {
        visible: !root.inactive
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        width: 10
        height: 10
        radius: 5
        color: root.surfaceColor
        Icon {
            anchors.centerIn: parent
            source: "qrc:/icons/check.svg"
            color: Theme.color.green
            size: 10
        }
    }
    HoverHandler { id: hover }
    ToolTip.visible: hover.hovered
    ToolTip.text: Accessible.name
}

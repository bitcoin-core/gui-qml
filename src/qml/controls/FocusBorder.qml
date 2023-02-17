// Copyright (c) 2023 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import "../controls"


 Rectangle {
    id: root
    property int topMargin: -4
    property int bottomMargin: -4
    property int leftMargin: -4
    property int rightMargin: -4
    property int borderRadius: 9

    anchors.fill: parent
    anchors.topMargin: root.topMargin
    anchors.bottomMargin: root.bottomMargin
    anchors.leftMargin: root.leftMargin
    anchors.rightMargin: root.rightMargin
    border.width: 2
    border.color: Theme.color.purple
    radius: root.borderRadius
    color: "transparent"

    Behavior on border.color {
        ColorAnimation { duration: 150 }
    }

    Behavior on visible {
        NumberAnimation { duration: 150 }
    }
}

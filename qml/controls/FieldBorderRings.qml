// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15

Item {
    id: root

    property color ringColor: Theme.color.neutral6
    property real cornerRadius: 10
    property string outerObjectName: ""
    property string innerObjectName: ""

    Rectangle {
        objectName: root.outerObjectName
        anchors.fill: parent
        anchors.margins: -3
        radius: root.cornerRadius + 3
        color: "transparent"
        border.width: 2
        border.color: Qt.rgba(root.ringColor.r, root.ringColor.g, root.ringColor.b, 0.3)
    }

    Rectangle {
        objectName: root.innerObjectName
        anchors.fill: parent
        anchors.margins: -1
        radius: root.cornerRadius + 1
        color: "transparent"
        border.width: 1
        border.color: Qt.rgba(root.ringColor.r, root.ringColor.g, root.ringColor.b, 0.5)
    }
}

// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15

Item {
    id: root

    property int thickness: 1
    property int verticalMargin: Theme.space.s
    property int horizontalInset: 10

    Layout.fillWidth: true
    Layout.preferredHeight: thickness + 2 * verticalMargin
    Layout.minimumHeight: thickness + 2 * verticalMargin
    implicitHeight: thickness + 2 * verticalMargin

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: root.horizontalInset
        anchors.rightMargin: root.horizontalInset
        anchors.verticalCenter: parent.verticalCenter
        height: root.thickness
        color: Theme.color.menuDivider
    }
}

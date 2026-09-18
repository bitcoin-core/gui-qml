// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15

Item {
    id: root
    required property color color
    required property url source
    property int iconSize: 22
    property int slotSize: 30

    implicitWidth: root.slotSize
    implicitHeight: root.slotSize
    Layout.preferredWidth: root.slotSize
    Layout.preferredHeight: root.slotSize
    Accessible.ignored: true

    Icon {
        anchors.centerIn: parent
        source: root.source
        color: root.color
        size: root.iconSize
    }
}

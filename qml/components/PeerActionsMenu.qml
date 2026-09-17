// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15

import "../controls"

ContextMenu {
    id: root

    signal banRequested(int duration, string label)
    signal disconnectRequested

    title: qsTr("Ban peer")
    minMenuWidth: 210

    ContextMenuButton {
        objectName: "peerBanDuration_3600"
        text: qsTr("1 hour")
        onTriggered: root.banRequested(3600, text)
    }
    ContextMenuButton {
        objectName: "peerBanDuration_86400"
        text: qsTr("1 day")
        onTriggered: root.banRequested(86400, text)
    }
    ContextMenuButton {
        objectName: "peerBanDuration_604800"
        text: qsTr("1 week")
        onTriggered: root.banRequested(604800, text)
    }
    ContextMenuButton {
        objectName: "peerBanDuration_31536000"
        text: qsTr("1 year")
        onTriggered: root.banRequested(31536000, text)
    }
    ContextMenuDivider { }
    ContextMenuButton {
        objectName: "peerDisconnectButton"
        text: qsTr("Disconnect")
        role: ContextMenuButton.Destructive
        onTriggered: root.disconnectRequested()
    }
}

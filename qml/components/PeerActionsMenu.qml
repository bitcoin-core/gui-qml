// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import org.bitcoincore.qt 1.0

import "../controls"

ContextMenu {
    id: root

    signal banRequested(int duration, string label)
    signal disconnectRequested

    property bool canBan: false
    property bool canDisconnect: false

    title: qsTr("Ban peer")
    minMenuWidth: 210

    ContextMenuButton {
        objectName: "peerBanDuration_3600"
        enabled: root.canBan
        text: qsTr("1 hour")
        onTriggered: root.banRequested(PeerCommandOperation.Hour, text)
    }
    ContextMenuButton {
        objectName: "peerBanDuration_86400"
        enabled: root.canBan
        text: qsTr("1 day")
        onTriggered: root.banRequested(PeerCommandOperation.Day, text)
    }
    ContextMenuButton {
        objectName: "peerBanDuration_604800"
        enabled: root.canBan
        text: qsTr("1 week")
        onTriggered: root.banRequested(PeerCommandOperation.Week, text)
    }
    ContextMenuButton {
        objectName: "peerBanDuration_31536000"
        enabled: root.canBan
        text: qsTr("1 year")
        onTriggered: root.banRequested(PeerCommandOperation.Year, text)
    }
    ContextMenuDivider { }
    ContextMenuButton {
        objectName: "peerDisconnectButton"
        enabled: root.canDisconnect
        text: qsTr("Disconnect")
        role: ContextMenuButton.Destructive
        onTriggered: root.disconnectRequested()
    }
}

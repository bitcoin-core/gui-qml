pragma ComponentBehavior: Bound

// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15

import "../../../controls"
import "../../node" as NodePages

Page {
    id: root
    objectName: "settingsv2RpcConsoleSettingsPage"

    property string walletName: ""
    property real maximumContentWidth: 840
    property real contentHorizontalPadding: width >= 900 ? 56 : width >= 640 ? 40 : 24
    readonly property alias consoleItem: rpcConsole

    background: null
    padding: 0
    clip: true

    header: SettingsHeader {
        objectName: "settingsv2RpcConsoleHeader"
        title: qsTr("RPC console")
        showBackButton: false
    }

    Item {
        id: contentFrame
        anchors {
            top: parent.top
            bottom: parent.bottom
            horizontalCenter: parent.horizontalCenter
            topMargin: 20
            bottomMargin: 20
        }
        width: Math.max(0, Math.min(
            parent.width - root.contentHorizontalPadding * 2,
            root.maximumContentWidth))

        NodePages.CommandConsole {
            id: rpcConsole
            objectName: "settingsv2RpcConsole"
            anchors.fill: parent
            showHeader: false
            tabActive: root.visible
            walletName: root.walletName
        }
    }
}

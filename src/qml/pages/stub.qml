// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Layouts 1.11
import "../components" as BitcoinCoreComponents


ApplicationWindow {
    id: appWindow
    title: "Bitcoin Core TnG"
    minimumWidth: 750
    minimumHeight: 450
    background: Rectangle {
        color: "black"
    }
    visible: true

    Component.onCompleted: initExecutor.initialize()

    Connections {
        target: initExecutor
        onInitializeResult: nodeModel.initializeResult(success, tip_info)
    }


    Image {
        id: appLogo
        anchors.horizontalCenter: parent.horizontalCenter
        source: "image://images/app"
        sourceSize.width: 128
        sourceSize.height: 128
    }

    BitcoinCoreComponents.BlockCounter {
        id: blockCounter
        anchors.centerIn: parent
        height: parent.height / 3
        blockHeight: nodeModel.blockTipHeight
    }
}

// Copyright (c) 2021-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BitcoinApp.Components

ApplicationWindow {
    id: appWindow
    title: "Bitcoin Core TnG"
    minimumWidth: 750
    minimumHeight: 450
    color: "black"
    visible: true

    Component.onCompleted: nodeModel.startNodeInitializionThread();

    Image {
        id: appLogo
        anchors.horizontalCenter: parent.horizontalCenter
        source: "image://images/app"
        sourceSize.width: 128
        sourceSize.height: 128
    }

    BlockCounter {
        id: blockCounter
        anchors.centerIn: parent
        blockHeight: nodeModel.blockTipHeight
    }
}

// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.12
import QtQuick.Controls 2.12
import "../components" as BitcoinCoreComponents


ApplicationWindow {
    id: appWindow
    title: "Bitcoin Core TnG"
    minimumWidth: 750
    minimumHeight: 450
    visible: true

    Component.onCompleted: nodeModel.startNodeInitializionThread();

    BitcoinCoreComponents.BlockCounter {
        id: blockCounter
        anchors.centerIn: parent
        height: parent.height / 3
        blockHeight: nodeModel.blockTipHeight
    }
}

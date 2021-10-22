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

    ColumnLayout {
        anchors.centerIn: parent
        spacing: 15
        width: 400
        Image {
            Layout.alignment: Qt.AlignCenter
            source: "image://images/app"
            sourceSize.width: 64
            sourceSize.height: 64
        }
        BlockCounter {
            Layout.alignment: Qt.AlignCenter
            blockHeight: nodeModel.blockTipHeight
        }
    }
}

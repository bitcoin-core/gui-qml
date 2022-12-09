// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BitcoinApp.Controls
import BitcoinApp.Components

Page {
    background: null
    clip: true
    property alias navRightDetail: navbar.rightDetail
    header: NavigationBar {
        id: navbar
    }
    ColumnLayout {
        spacing: 0
        anchors.fill: parent
        ColumnLayout {
            width: 600
            spacing: 0
            anchors.centerIn: parent
            Component.onCompleted: nodeModel.startNodeInitializionThread();
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
            ProgressIndicator {
                width: 200
                Layout.alignment: Qt.AlignCenter
                progress: nodeModel.verificationProgress
            }
        }
    }
}

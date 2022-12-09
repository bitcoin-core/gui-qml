// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../../controls"
import "../../components"

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

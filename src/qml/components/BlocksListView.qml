// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import BitcoinCore 1.0
import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Layouts 1.11
import "../controls"

ListView {
    spacing: 15
    model: ChainModel {
    }
    delegate: ItemDelegate {
        id: delegate
        padding: 15
        background: Rectangle {
            border.width: 1
            border.color: delegate.hovered ? "white" : "#999999"
            radius: 10
            color: "transparent"
        }
        contentItem: ColumnLayout {
            spacing: 3
            Label {
                color: "white"
                font.family: "Inter"
                font.styleName: "Regular"
                font.pixelSize: 28
                text: qsTrId('Block #%1').arg(blockHeight)
            }
            Label {
                Layout.fillWidth: true
                Layout.preferredWidth: 0
                color: "white"
                elide: Text.ElideRight
                font.family: "Inter"
                font.styleName: "Regular"
                font.pixelSize: 13
                text: blockHash
            }
        }
    }
}

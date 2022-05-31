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
    Layout.fillWidth: true
    clip: true
    header: RowLayout {
        height: 50
        Loader {
            active: true
            visible: active
            Layout.alignment: Qt.AlignRight
            Layout.topMargin: 12
            Layout.rightMargin: -7
            sourceComponent: Item {
                Layout.fillWidth: true
                width: 73
                height: 46
                Text {
                    Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                    text: "Done"
                    color: Theme.color.neutral9
                    font.family: "Inter"
                    font.styleName: "Semi Bold"
                    font.pixelSize: 18
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        storages.decrementCurrentIndex()
                        swipeView.inSubPage = false
                    }
                }
            }
        }
    }
    ColumnLayout {
        width: 450
        spacing: 0
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        Header {
            Layout.fillWidth: true
            bold: true
            header: "Storage settings"
        }
        StorageSettings {
            Layout.topMargin: 30
        }
    }
}

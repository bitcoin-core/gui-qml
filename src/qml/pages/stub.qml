// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import BitcoinCore 1.0
import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Layouts 1.11
import "../components"
import "../controls"

ApplicationWindow {
    id: appWindow
    title: "Bitcoin Core TnG"
    minimumWidth: 750
    minimumHeight: 450
    color: "black"
    visible: true

    Component.onCompleted: initExecutor.initialize()

    Connections {
        target: initExecutor
        onInitializeResult: nodeModel.initializeResult(success, tip_info)
    }

    BusyIndicator {
        running: !initExecutor.ready
        visible: running
        anchors.centerIn: parent
    }

    Loader {
        id: loader
        active: initExecutor.ready
        anchors.centerIn: parent
        sourceComponent: ColumnLayout {
            spacing: 15
            width: 400
            NodeModel {
                id: nodeModel
            }
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
                id: indicator
                Layout.fillWidth: true
                progress: 0.666
                background: MouseArea {
                    onClicked: indicator.progress = mouseX / width
                }
            }
            ConnectionOptions {
                Layout.preferredWidth: 400
                focus: true
            }
        }
    }
}

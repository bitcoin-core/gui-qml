// Copyright (c) 2023 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import "../controls"
import "../components"

import org.bitcoincore.qt 1.0

Button {
    id: root
    property color bgColor
    property int textSize: 15
    topPadding: textSize * (2/15)
    bottomPadding: textSize * (2/15)
    leftPadding: textSize * (7/15)
    rightPadding: textSize * (7/15)
    state: chainModel.currentNetworkName
    contentItem: CoreText {
        text: root.text
        font.pixelSize: root.textSize
        color: Theme.color.white
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
    background: Rectangle {
        id: bg
        color: root.bgColor
        radius: textSize * (2/15)

        Behavior on color {
            ColorAnimation { duration: 150 }
        }
    }
    states: [
        State {
            name: "MAIN"
            PropertyChanges {
                target: root
                visible: false
            }
        },
        State {
            name: "TEST"
            PropertyChanges {
                target: root
                visible: true
                text: qsTr("Test Network")
                bgColor: Theme.color.green
            }
        },
        State {
            name: "SIGNET"
            PropertyChanges {
                target: root
                visible: true
                text: qsTr("Signet Network")
                bgColor: Theme.color.amber
            }
        },
        State {
            name: "REGTEST"
            PropertyChanges {
                target: root
                visible: true
                text: qsTr("Regtest Mode")
                bgColor: Theme.color.blue
            }
        }
    ]
}

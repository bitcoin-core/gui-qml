// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15

Button {
    id: root
    font.family: "Inter"
    font.styleName: "Semi Bold"
    font.pixelSize: 18
    hoverEnabled: true
    contentItem: Text {
        text: parent.text
        font: parent.font
        color: Theme.color.white
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
    background: Rectangle {
        id: bg
        implicitHeight: 46
        implicitWidth: 300
        color: Theme.color.orange
        radius: 5

        states: [
            State {
                name: "PRESSED"; when: root.pressed
                PropertyChanges { target: bg; color: Theme.color.orangeLight2 }
            },
            State {
                name: "HOVER"; when: root.hovered
                PropertyChanges { target: bg; color: Theme.color.orangeLight1 }
            }
        ]

        Behavior on color {
            ColorAnimation { duration: 150 }
        }
    }
}

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
        color: Theme.color.neutral9
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
    background: Rectangle {
        id: bg
        implicitHeight: 46
        implicitWidth: 340
        color: Theme.color.background
        radius: 5
        border {
            width: 1
            color: Theme.color.neutral6

            Behavior on color {
                ColorAnimation { duration: 150 }
            }
        }
    }

    states: [
        State {
            name: "PRESSED"; when: root.pressed
            PropertyChanges { target: bg; border.color: Theme.color.orangeLight2 }
        },
        State {
            name: "HOVER"; when: root.hovered
            PropertyChanges { target: bg; border.color: Theme.color.neutral9 }
        }
    ]
}

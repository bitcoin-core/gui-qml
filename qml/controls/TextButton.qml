// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15

Button {
    id: root
    property int textSize: 18
    property color textColor: Theme.color.orange
    property color bgColor: Theme.color.background
    property bool bold: true
    property bool rightalign: false
    font.family: "Inter"
    font.styleName: bold ? "Semi Bold" : "Regular"
    font.pixelSize: root.textSize
    padding: 15
    hoverEnabled: true
    contentItem: Text {
        text: root.text
        font: root.font
        color: root.textColor
        horizontalAlignment: rightalign ? Text.AlignRight : Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        Behavior on color {
            ColorAnimation { duration: 150 }
        }
    }
    background: Rectangle {
        id: bg
        color: root.bgColor
        radius: 5
        Behavior on color {
            ColorAnimation { duration: 150 }
        }
    }
    states: [
        State {
            name: "PRESSED"; when: root.pressed
            PropertyChanges {
                target: root
                textColor: Theme.color.orangeLight2
                bgColor: Theme.color.neutral3
            }
        },
        State {
            name: "HOVER"; when: root.hovered
            PropertyChanges {
                target: root
                textColor: Theme.color.orangeLight1
                bgColor: Theme.color.neutral2
            }
        }
    ]
}

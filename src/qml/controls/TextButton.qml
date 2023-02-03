// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15

Button {
    id: root
    property int textSize: 18
    property color textColor
    property color bgColor
    property bool bold: true
    property bool rightalign: false
    font.family: "Inter"
    font.styleName: bold ? "Semi Bold" : "Regular"
    font.pixelSize: root.textSize
    padding: 15
    state: "DEFAULT"
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
            name: "DEFAULT"
            PropertyChanges {
                target: root
                textColor: Theme.color.orange
                bgColor: Theme.color.background
            }
        },
        State {
            name: "HOVER"
            PropertyChanges {
                target: root
                textColor: Theme.color.orangeLight1
                bgColor: Theme.color.neutral2
            }
        },
        State {
            name: "PRESSED"
            PropertyChanges {
                target: root
                textColor: Theme.color.orangeLight2
                bgColor: Theme.color.neutral3
            }
        }
    ]
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        onEntered: {
            root.state = "HOVER"
        }
        onExited: {
            root.state = "DEFAULT"
        }
        onPressed: {
            root.state = "PRESSED"
        }
        onReleased: {
            root.state = "DEFAULT"
            root.clicked()
        }
    }
}

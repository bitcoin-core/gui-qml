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
        state:"DEFAULT"
        border {
            width: 1
            Behavior on color {
                ColorAnimation { duration: 150 }
            }
        }

        states: [
            State {
                name: "DEFAULT"
                PropertyChanges { target: bg; border.color: Theme.color.neutral6}
            },
            State {
                name: "HOVER"
                PropertyChanges { target: bg; border.color: Theme.color.neutral9 }
            },
            State {
                name: "PRESSED"
                PropertyChanges { target: bg; border.color: Theme.color.orangeLight2 }
            }
        ]
    }
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        onEntered: {
            root.background.state = "HOVER"
        }
        onExited: {
            root.background.state = "DEFAULT"
        }
        onPressed: {
            root.background.state = "PRESSED"
        }
        onReleased: {
            root.background.state = "DEFAULT"
            root.clicked()
        }
    }
}

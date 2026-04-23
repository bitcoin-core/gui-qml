// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import org.bitcoincore.qt 1.0

Button {
    id: root
    hoverEnabled: AppMode.isDesktop

    property bool bold: true
    property url iconSource: ""

    leftPadding: 20
    rightPadding: 20

    HoverHandler { cursorShape: Qt.PointingHandCursor }

    contentItem: Item {
        implicitWidth: row.implicitWidth
        implicitHeight: row.implicitHeight
        Row {
            id: row
            anchors.centerIn: parent
            spacing: 4
            Icon {
                id: icon
                anchors.verticalCenter: parent.verticalCenter
                visible: root.iconSource.toString().length > 0
                source: root.iconSource
                color: Theme.color.neutral9
                size: 20
            }
            CoreText {
                id: label
                anchors.verticalCenter: parent.verticalCenter
                text: root.text
                bold: root.bold
                font.pixelSize: 18
                color: Theme.color.neutral9
            }
        }
    }
    background: Rectangle {
        id: bg
        implicitHeight: 46
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
            name: "DISABLED"; when: !root.enabled
            PropertyChanges { target: bg; border.color: Theme.color.neutral4 }
            PropertyChanges { target: icon; color: Theme.color.neutral4 }
            PropertyChanges { target: label; color: Theme.color.neutral4 }
        },
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

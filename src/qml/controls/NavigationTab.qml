// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import org.bitcoincore.qt 1.0

Button {
    property color bgActiveColor: Theme.color.orange
    property color textColor: Theme.color.neutral7
    property color textHoverColor: Theme.color.neutral9
    property color textActiveColor: Theme.color.orange
    property color iconColor: "transparent"
    property string iconSource: ""

    id: root
    checkable: true
    hoverEnabled: AppMode.isDesktop
    implicitHeight: 60
    implicitWidth: 80
    bottomPadding: 0
    topPadding: 0

    contentItem: Item {
        width: parent.width
        height: parent.height
        CoreText {
            id: buttonText
            font.pixelSize: 15
            text: root.text
            color: root.textColor
            bold: true
            visible: root.text !== ""
            anchors.centerIn: parent
        }
        Icon {
            id: icon
            source: root.iconSource
            color: iconColor
            visible: root.iconSource !== ""
            anchors.centerIn: parent
        }
    }

    background: Item {
        Rectangle {
            id: bg
            height: parent.height - 5
            width: parent.width
            radius: 5
            color: Theme.color.neutral3
            visible: root.hovered

            FocusBorder {
                visible: root.visualFocus
            }

            Behavior on color {
                ColorAnimation { duration: 150 }
            }
        }
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 3
            visible: root.checked
            color: root.bgActiveColor
        }
    }

    states: [
        State {
            name: "CHECKED"; when: root.checked
            PropertyChanges { target: buttonText; color: root.textActiveColor }
            PropertyChanges { target: icon; color: root.textActiveColor }
        },
        State {
            name: "HOVER"; when: root.hovered
            PropertyChanges { target: buttonText; color: root.textHoverColor }
            PropertyChanges { target: icon; color: root.textHoverColor }
        }
    ]
}

// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import org.bitcoincore.qt 1.0

Button {
    id: root
    hoverEnabled: AppMode.isDesktop

    property color textColor: Theme.color.white
    property color backgroundColor: Theme.color.orange
    property color backgroundHoverColor: Theme.color.orangeLight1
    property color backgroundPressedColor: Theme.color.orangeLight2
    property color borderColor: "transparent"
    property color borderHoverColor: "transparent"
    property color borderPressedColor: "transparent"

    contentItem: CoreText {
        text: parent.text
        color: root.textColor
        bold: true
        font.pixelSize: 18
    }
    background: Rectangle {
        id: bg
        implicitHeight: 46
        color: backgroundColor
        border.color: borderColor
        radius: 5

        states: [
            State {
                name: "PRESSED"; when: root.pressed
                PropertyChanges { target: bg; color: backgroundPressedColor }
                PropertyChanges { target: bg; border.color: borderPressedColor }
            },
            State {
                name: "HOVER"; when: root.hovered
                PropertyChanges { target: bg; color: backgroundHoverColor }
                PropertyChanges { target: bg; border.color: borderHoverColor }
            }
        ]

        Behavior on color {
            ColorAnimation { duration: 150 }
        }

        FocusBorder {
            visible: root.visualFocus
        }
    }
}

// Copyright (c) 2023 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import org.bitcoincore.qt 1.0

Button {
    property int bgRadius: 5
    property color bgDefaultColor: Theme.color.neutral2
    property color bgHoverColor: Theme.color.neutral2
    property color bgActiveColor: Theme.color.neutral5
    property color textColor: Theme.color.neutral7
    property color textHoverColor: Theme.color.orangeLight1
    property color textActiveColor: Theme.color.neutral9

    id: root
    checkable: true
    hoverEnabled: AppMode.isDesktop
    leftPadding: 12
    rightPadding: 12
    topPadding: 5
    bottomPadding: 5

    contentItem: CoreText {
        id: buttonText
        text: parent.text
        font.pixelSize: 13
        color: root.textColor

        Behavior on color {
            ColorAnimation { duration: 150 }
        }
    }

    background: Rectangle {
        id: bg
        color: root.bgDefaultColor
        radius: root.bgRadius

        Behavior on color {
            ColorAnimation { duration: 150 }
        }
    }

    states: [
        State {
            name: "CHECKED"; when: root.checked
            PropertyChanges { target: bg; color: root.bgActiveColor }
            PropertyChanges { target: buttonText; color: root.textActiveColor }
        },
        State {
            name: "HOVER"; when: root.hovered
            PropertyChanges { target: bg; color: root.bgHoverColor }
            PropertyChanges { target: buttonText; color: root.textHoverColor }
        }
    ]
}

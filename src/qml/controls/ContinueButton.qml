// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import org.bitcoincore.qt 1.0

Button {
    id: root
    hoverEnabled: AppMode.isDesktop
    contentItem: CoreText {
        text: parent.text
        bold: true
        font.pixelSize: 18
    }
    background: Rectangle {
        id: bg
        implicitHeight: 46
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

        FocusBorder {
            visible: root.visualFocus
        }
    }
}

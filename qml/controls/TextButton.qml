// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15

import org.bitcoincore.qt 1.0

Button {
    id: root
    property var textStyle: Theme.text.buttonStrong
    property color textColor: enabled ? Theme.color.accentText : Theme.color.disabledContent
    property color bgColor: "transparent"
    property bool rightalign: false
    padding: Theme.space.l
    hoverEnabled: AppMode.isDesktop
    contentItem: CoreText {
        text: root.text
        textStyle: root.textStyle
        color: root.textColor
        horizontalAlignment: rightalign ? Text.AlignRight : Text.AlignHCenter
        Behavior on color {
            enabled: Theme.motion.animated
            ColorAnimation { duration: Theme.motion.normal }
        }
    }
    background: StateBackground {
        id: bg
        stateColor: root.bgColor
        radius: Theme.radius.s

        FocusBorder {
            visible: root.visualFocus
        }
    }
    states: [
        State {
            name: "PRESSED"; when: root.pressed
            PropertyChanges {
                target: root
                textColor: Theme.color.accentPressed
                bgColor: Theme.color.surfacePressed
            }
        },
        State {
            name: "HOVER"; when: root.hovered
            PropertyChanges {
                target: root
                textColor: Theme.color.accentHover
                bgColor: Theme.color.surfaceHover
            }
        }
    ]
}

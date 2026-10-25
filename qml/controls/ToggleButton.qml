// Copyright (c) 2023 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import org.bitcoincore.qt 1.0

Button {
    property int bgRadius: Theme.radius.s
    property color bgDefaultColor: Theme.color.neutral2
    property color bgHoverColor: Theme.color.neutral2
    property color bgActiveColor: Theme.color.neutral5
    property color textColor: Theme.color.neutral7
    property color textHoverColor: Theme.color.orangeLight1
    property color textActiveColor: Theme.color.neutral9
    property bool textBold: false
    property bool textActiveBold: textBold

    id: root
    checkable: true
    hoverEnabled: AppMode.isDesktop
    focusPolicy: Qt.StrongFocus
    leftPadding: Theme.space.ml
    rightPadding: Theme.space.ml
    topPadding: 5
    bottomPadding: 5

    contentItem: CoreText {
        id: buttonText
        text: parent.text
        font.pixelSize: Theme.text.caption.pixelSize
        bold: root.textBold
        color: root.textColor

        Behavior on color {
            enabled: Theme.motion.animated
            ColorAnimation { duration: Theme.motion.normal }
        }
    }

    background: Rectangle {
        id: bg
        color: root.bgDefaultColor
        radius: root.bgRadius

        Behavior on color {
            enabled: Theme.motion.animated
            ColorAnimation { duration: Theme.motion.normal }
        }
    }

    FocusBorder {
        objectName: root.objectName.length > 0 ? root.objectName + "FocusBorder" : ""
        visible: root.visualFocus
        borderRadius: root.bgRadius + 4
        z: 1
    }

    states: [
        State {
            name: "CHECKED"; when: root.checked
            PropertyChanges { target: bg; color: root.bgActiveColor }
            PropertyChanges { target: buttonText; color: root.textActiveColor }
            PropertyChanges { target: buttonText; bold: root.textActiveBold }
        },
        State {
            name: "HOVER"; when: root.hovered
            PropertyChanges { target: bg; color: root.bgHoverColor }
            PropertyChanges { target: buttonText; color: root.textHoverColor }
        }
    ]
}

// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import org.bitcoincore.qt 1.0

Button {
    id: root
    hoverEnabled: AppMode.isDesktop

    property bool bold: false
    property url iconSource: ""
    property var textStyle: bold ? Theme.text.buttonStrong : Theme.text.button
    property int textFontPixelSize: textStyle.pixelSize
    property string textFontStyleName: textStyle.styleName

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
                fontStyleName: root.textFontStyleName
                font.pixelSize: root.textFontPixelSize
                color: Theme.color.neutral9
            }
        }
    }
    background: Rectangle {
        id: bg
        implicitHeight: 46
        color: "transparent"
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
            name: "PRESSED"; when: root.pressed
            PropertyChanges { target: bg; border.color: Theme.color.orangeLight2 }
        },
        State {
            name: "HOVER"; when: root.hovered
            PropertyChanges { target: bg; border.color: Theme.color.neutral9 }
        }
    ]
}

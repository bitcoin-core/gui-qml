// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

AbstractButton {
    id: root
    property int iconHeight: 30
    property int iconWidth: 30
    property int textSize: 18
    property url iconSource: ""
    property Rectangle iconBackground: null
    property color iconColor: Theme.color.neutral9

    topPadding: text_background.active ? 7 : 14
    bottomPadding: text_background.active ? 7 : 14
    rightPadding: text_background.active ? 22 : 14
    leftPadding: text_background.active ? 2 : 14
    background: Rectangle {
        id: bg
        height: root.height
        width: root.width
        radius: 5
        state:"DEFAULT"

        states: [
            State {
                name: "DEFAULT"
                PropertyChanges { target: bg; color: Theme.color.background }
            },
            State {
                name: "HOVER"
                PropertyChanges { target: bg; color: Theme.color.neutral2 }
            },
            State {
                name: "PRESSED"
                PropertyChanges { target: bg; color: Theme.color.neutral3 }
            }
        ]

        FocusBorder {
            visible: root.visualFocus
        }

        Behavior on color {
            ColorAnimation { duration: 150 }
        }
    }
    contentItem: RowLayout {
        spacing: 0
        Loader {
           id: button_background
           active: root.iconSource.toString().length > 0
           visible: active
           sourceComponent: Button {
               id: icon_button
               padding: 0
               display: AbstractButton.IconOnly
               height: root.iconHeight
               width: root.iconWidth
               icon.source: root.iconSource
               icon.color: root.iconColor
               icon.height: root.iconHeight
               icon.width: root.iconWidth
               background: root.iconBackground
           }
        }
        Loader {
            id: text_background
            active: root.text.length > 0
            visible: active
            sourceComponent: AbstractButton {
                id: container
                topPadding: button_background.active ? 0 : 4
                bottomPadding: button_background.active ? 0 : 4
                rightPadding: 0
                leftPadding: button_background.active ? 0 : 20
                font.family: "Inter"
                font.styleName: "Semi Bold"
                font.pixelSize: root.textSize
                contentItem: Text {
                    anchors.verticalCenter: parent.verticalCenter
                    font: container.font
                    color: Theme.color.neutral9
                    text: root.text
                }
            }
        }
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

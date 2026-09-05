// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

Button {
    id: root
    hoverEnabled: AppMode.isDesktop

    property color textColor: Theme.color.white
    property color textHoverColor: textColor
    property color textPressedColor: textColor
    property color backgroundColor: Theme.color.orange
    property color backgroundHoverColor: Theme.color.orangeLight1
    property color backgroundPressedColor: Theme.color.orangeLight2
    property color borderColor: "transparent"
    property color borderHoverColor: "transparent"
    property color borderPressedColor: "transparent"
    property bool bold: true
    property bool busy: false
    property url iconSource: ""
    property var textStyle: bold ? Theme.text.buttonStrong : Theme.text.button
    property int textFontPixelSize: textStyle.pixelSize
    property string textFontStyleName: textStyle.styleName

    contentItem: Item {
        implicitHeight: contentRow.implicitHeight

        RowLayout {
            id: contentRow
            anchors.centerIn: parent
            spacing: 4

            SpinningIndicator {
                Layout.alignment: Qt.AlignVCenter
                visible: root.busy
                running: root.busy
                color: root.textColor
            }

            Icon {
                Layout.alignment: Qt.AlignVCenter
                visible: !root.busy && root.iconSource.toString() !== ""
                source: root.iconSource
                color: root.textColor
                size: 24
            }

            CoreText {
                Layout.alignment: Qt.AlignVCenter
                text: root.text
                color: root.textColor
                bold: root.bold
                fontStyleName: root.textFontStyleName
                font.pixelSize: root.textFontPixelSize
            }
        }
    }
    background: Rectangle {
        id: bg
        implicitHeight: 46
        color: backgroundColor
        border.color: borderColor
        radius: 5

        states: [
            State {
                name: "DISABLED"; when: !root.enabled
                PropertyChanges { target: bg; color: Theme.color.neutral2 }
                PropertyChanges { target: bg; border.color: Theme.color.neutral2 }
                PropertyChanges { target: root; textColor: Theme.color.neutral5 }
            },
            State {
                name: "PRESSED"; when: root.pressed
                PropertyChanges { target: bg; color: backgroundPressedColor }
                PropertyChanges { target: bg; border.color: borderPressedColor }
                PropertyChanges { target: root; textColor: textPressedColor }
            },
            State {
                name: "HOVER"; when: root.hovered
                PropertyChanges { target: bg; color: backgroundHoverColor }
                PropertyChanges { target: bg; border.color: borderHoverColor }
                PropertyChanges { target: root; textColor: textHoverColor }
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

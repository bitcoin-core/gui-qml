// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import org.bitcoincore.qt 1.0

AbstractButton {
    id: root
    property int iconHeight: 30
    property int iconWidth: 30
    property bool bold: true
    property bool busy: false
    property var textStyle: bold ? Theme.text.buttonStrong : Theme.text.button
    property url iconSource: ""
    property color iconColor: enabled ? Theme.color.neutral9 : Theme.color.neutral2
    hoverEnabled: AppMode.isDesktop && !busy
    topPadding: text_background.active ? 7 : 14
    bottomPadding: text_background.active ? 7 : 14
    rightPadding: text_background.active ? 22 : 14
    leftPadding: text_background.active ? 2 : 14

    HoverHandler {
        enabled: root.enabled && root.hoverEnabled
        cursorShape: Qt.PointingHandCursor
    }

    background: Rectangle {
        id: bg
        height: root.height
        width: root.width
        radius: Theme.radius.s
        color: root.down ? Theme.color.neutral3
            : root.hovered ? Theme.color.neutral2
            : Theme.color.background

        FocusBorder {
            visible: root.visualFocus
        }

        Behavior on color {
            enabled: Theme.motion.animated
            ColorAnimation { duration: Theme.motion.normal }
        }
    }
    contentItem: RowLayout {
        spacing: 0
        Item {
            Layout.fillWidth: !text_background.active
        }
        Loader {
           id: button_background
           active: root.iconSource.toString().length > 0
           visible: active
           sourceComponent: Icon {
               source: root.iconSource
               color: root.iconColor
               size: root.iconHeight
               icon.width: root.iconWidth
           }
        }
        Item {
            Layout.fillWidth: !text_background.active
        }
        SpinningIndicator {
            Layout.alignment: Qt.AlignVCenter
            visible: root.busy
            running: root.busy
            color: root.iconColor
        }
        Loader {
            id: text_background
            active: root.text.length > 0 && !root.busy
            visible: active
            sourceComponent: CoreText {
                topPadding: button_background.active ? 0 : 4
                bottomPadding: button_background.active ? 0 : 4
                leftPadding: button_background.active ? 0 : 20
                textStyle: root.textStyle
                text: root.text
                Accessible.ignored: true
            }
        }
    }
}

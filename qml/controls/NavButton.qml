// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import org.bitcoincore.qt 1.0

AbstractButton {
    id: root
    // One back-caret size across top bars.
    property int iconHeight: Theme.size.icon.xl
    property int iconWidth: Theme.size.icon.xl
    property string iconObjectName: ""
    readonly property bool iconOnly: !text_background.active && button_background.active
    property bool bold: true
    property bool busy: false
    property var textStyle: bold ? Theme.text.buttonStrong : Theme.text.button
    property url iconSource: ""
    property color iconColor: enabled ? Theme.color.neutral9 : Theme.color.disabledContent
    hoverEnabled: AppMode.isDesktop && !busy
    // An icon-only button is a square Theme.size.iconButton target.
    topPadding: text_background.active ? 7 : iconOnly ? (Theme.size.iconButton - iconHeight) / 2 : 14
    bottomPadding: topPadding
    rightPadding: text_background.active ? 22 : iconOnly ? (Theme.size.iconButton - iconWidth) / 2 : 14
    leftPadding: text_background.active ? 2 : iconOnly ? rightPadding : 14

    HoverHandler {
        enabled: root.enabled && root.hoverEnabled
        cursorShape: Qt.PointingHandCursor
    }

    background: StateBackground {
        id: bg
        height: root.height
        width: root.width
        radius: Theme.radius.s
        stateColor: root.down ? Theme.color.surfacePressed
            : root.hovered ? Theme.color.surfaceHover
            : "transparent"

        FocusBorder {
            visible: root.visualFocus
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
               objectName: root.iconObjectName
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

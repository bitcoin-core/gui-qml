// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import org.bitcoincore.qt 1.0

TabButton {
    property color bgActiveColor: Theme.color.orange
    property color textColor: Theme.color.neutral7
    property color textHoverColor: Theme.color.neutral9
    property color textActiveColor: Theme.color.orange
    property color iconColor: "transparent"
    property string iconSource: ""
    property int iconSize: 30
    property Component customContent: null

    id: root
    checkable: true
    Accessible.role: Accessible.PageTab
    hoverEnabled: AppMode.isDesktop
    implicitHeight: 60
    implicitWidth: 80
    bottomPadding: 0
    topPadding: 0

    HoverHandler {
        cursorShape: root.enabled && root.hoverEnabled ? Qt.PointingHandCursor : Qt.ArrowCursor
    }

    contentItem: Item {
        width: parent.width
        height: parent.height
        Loader {
            id: customContentLoader
            active: root.customContent !== null
            visible: active
            sourceComponent: root.customContent
            anchors.centerIn: parent
        }
        CoreText {
            id: buttonText
            textStyle: Theme.text.subheading
            text: root.text
            color: root.textColor
            visible: !customContentLoader.active && root.text !== ""
            anchors.centerIn: parent
        }
        Icon {
            id: icon
            source: root.iconSource
            color: iconColor
            size: root.iconSize
            visible: !customContentLoader.active && root.iconSource !== ""
            anchors.centerIn: parent
        }
    }

    background: Item {
        Rectangle {
            id: bg
            height: parent.height - 5
            width: parent.width
            radius: Theme.radius.s
            color: Theme.color.neutral3
            visible: root.hovered

            Behavior on color {
                enabled: Theme.motion.animated
                ColorAnimation { duration: Theme.motion.normal }
            }
        }
        FocusBorder {
            anchors.fill: bg
            visible: root.visualFocus
            targetRadius: bg.radius
        }
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 3
            visible: root.checked
            color: root.bgActiveColor
        }
    }

    states: [
        State {
            name: "CHECKED"; when: root.checked
            PropertyChanges { target: buttonText; color: root.textActiveColor }
            PropertyChanges { target: icon; color: root.textActiveColor }
        },
        State {
            name: "HOVER"; when: root.hovered
            PropertyChanges { target: buttonText; color: root.textHoverColor }
            PropertyChanges { target: icon; color: root.textHoverColor }
        }
    ]
}

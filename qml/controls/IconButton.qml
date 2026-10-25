// Copyright (c) 2025-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import org.bitcoincore.qt 1.0

Button {
    id: root

    implicitHeight: root.height
    implicitWidth: root.width
    // A toolbar button rests on a surface1 fill next to search and filter
    // controls; the default is a bare icon with a hover tint.
    property bool toolbar: false
    property int surfaceLevel: 0
    property color iconColor: toolbar ? Theme.color.neutral9 : Theme.color.neutral5
    property color hoverColor: Theme.color.neutral9
    // A checked toolbar button is an applied toggle: inverted neutral.
    property color activeColor: toolbar ? Theme.color.activeForeground : Theme.color.accent
    property int size: toolbar ? Theme.size.iconButton : 35
    // Artwork size is a token of its own, not the target size.
    property int iconSize: toolbar ? Theme.size.icon.l : Theme.size.icon.xl
    property int backgroundRadius: toolbar ? Theme.radius.m : Theme.radius.s
    property alias iconSource: icon.source
    required property string accessibleName

    hoverEnabled: AppMode.isDesktop
    focusPolicy: Qt.StrongFocus
    height: root.size
    width: root.size
    padding: 0
    Accessible.name: root.accessibleName

    HoverHandler {
        cursorShape: Qt.PointingHandCursor
    }

    background: StateBackground {
        id: bg
        anchors.fill: parent
        radius: root.backgroundRadius
        stateColor: root.toolbar
            ? (root.checked ? Theme.color.activeFill
                : root.pressed ? Theme.color.surface(root.surfaceLevel + 3)
                : root.hovered ? Theme.color.surface(root.surfaceLevel + 2)
                : Theme.color.surface(root.surfaceLevel + 1))
            : root.pressed ? Theme.color.surfacePressed
            : root.hovered ? Theme.color.surfaceHover
            : "transparent"
    }

    FocusBorder {
        objectName: root.objectName.length > 0 ? root.objectName + "FocusBorder" : ""
        visible: root.visualFocus
        targetRadius: root.backgroundRadius
        z: 1
    }

    contentItem: Icon {
        id: icon
        anchors.fill: parent
        source: ""
        size: root.iconSize
        color: root.iconColor
        hoverEnabled: false

        Behavior on color {
            enabled: Theme.motion.animated
            ColorAnimation { duration: Theme.motion.normal }
        }
    }

    // The first state whose condition holds wins.
    states: [
        State {
            name: "DISABLED"; when: !root.enabled
            PropertyChanges { target: icon; color: Theme.color.disabledContent }
        },
        State {
            name: "CHECKED"; when: root.checked
            PropertyChanges { target: icon; color: root.activeColor }
        },
        State {
            name: "PRESSED"; when: root.pressed
            PropertyChanges { target: icon; color: root.hoverColor }
        },
        State {
            name: "HOVER"; when: root.hovered
            PropertyChanges { target: icon; color: root.hoverColor }
        }
    ]
}

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
    property color iconColor: Theme.color.neutral5
    property color hoverColor: activeColor
    property color activeColor: Theme.color.orange
    property int size: 35
    property int iconSize: size
    property int backgroundRadius: 5
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

    background: Rectangle {
        id: bg
        anchors.fill: parent
        radius: root.backgroundRadius
        color: root.hovered || root.pressed ? Theme.color.neutral2 : Theme.color.background

        Behavior on color {
            ColorAnimation { duration: 150 }
        }
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
            ColorAnimation { duration: 150 }
        }
    }

    states: [
        State {
            name: "HOVER"; when: root.hovered
            PropertyChanges { target: icon; color: root.hoverColor }
        },
        State {
            name: "CHECKED"; when: root.checked
            PropertyChanges { target: icon; color: root.activeColor }
        },
        State {
            name: "PRESSED"; when: root.pressed
            PropertyChanges { target: icon; color: root.activeColor }
        },
        State {
            name: "DISABLED"; when: !root.enabled
            PropertyChanges { target: icon; color: Theme.color.neutral4 }
        }
    ]
}

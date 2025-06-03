// Copyright (c) 2025 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import org.bitcoincore.qt 1.0

Button {
    id: root

    property color iconColor: Theme.color.orange
    property color hoverColor: Theme.color.orange
    property color activeColor: Theme.color.orange
    property int size: 35
    property alias iconSource: icon.source

    hoverEnabled: AppMode.isDesktop
    height: root.size
    width: root.size
    padding: 0

    MouseArea {
        anchors.fill: parent
        enabled: false
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
    }

    background: Rectangle {
        id: bg
        anchors.fill: parent
        radius: 5
        color: Theme.color.background


        Behavior on color {
            ColorAnimation { duration: 150 }
        }
    }

    contentItem: Icon {
        id: icon
        anchors.fill: parent
        source: "image://images/ellipsis"
        size: root.size
        color: iconColor

        Behavior on color {
            ColorAnimation { duration: 150 }
        }
    }

    states: [
        State {
            name: "CHECKED"; when: root.checked
            PropertyChanges { target: icon; color: activeColor }
        },
        State {
            name: "HOVER"; when: root.hovered
            PropertyChanges { target: icon; color: hoverColor }
            PropertyChanges { target: bg; color: Theme.color.neutral2 }
        },
        State {
            name: "DISABLED"; when: !root.enabled
            PropertyChanges { target: icon; color: Theme.color.neutral2 }
            PropertyChanges { target: bg; color: Theme.color.background }
        }
    ]
}

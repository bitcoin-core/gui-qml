// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import org.bitcoincore.qt 1.0

Button {
    id: root

    property color hoverColor: Theme.color.orange
    property color activeColor: Theme.color.orange

    hoverEnabled: AppMode.isDesktop
    implicitHeight: 35
    implicitWidth: 35

    MouseArea {
        anchors.fill: parent
        enabled: false
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
    }

    background: null

    contentItem: Icon {
        id: ellipsisIcon
        anchors.fill: parent
        source: "image://images/ellipsis"
        color: Theme.color.neutral9
        size: 35
    }

    states: [
        State {
            name: "CHECKED"; when: root.checked
            PropertyChanges { target: ellipsisIcon; color: activeColor }
        },
        State {
            name: "HOVER"; when: root.hovered
            PropertyChanges { target: ellipsisIcon; color: hoverColor }
        }
    ]
}

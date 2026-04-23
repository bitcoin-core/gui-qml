// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

Button {
    id: root

    hoverEnabled: AppMode.isDesktop

    implicitWidth: 280
    implicitHeight: 44

    HoverHandler { cursorShape: Qt.PointingHandCursor }

    contentItem: RowLayout {
        spacing: 7
        anchors.fill: parent
        anchors.margins: 10
        CoreText {
            id: buttonText
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            horizontalAlignment: Text.AlignLeft
            font.pixelSize: 15
            text: root.text
        }
    }

    background: Rectangle {
        id: bg
        color: "transparent"
        radius: 5

        Behavior on color {
            ColorAnimation { duration: 150 }
        }
    }

    states: [
        State {
            name: "HOVER"; when: root.hovered && root.enabled
            PropertyChanges { target: buttonText; color: Theme.color.orange }
        }
    ]
}

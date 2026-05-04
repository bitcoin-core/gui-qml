// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import "../controls"

Rectangle {
    id: root

    property bool shown: false
    property int visibleDurationMs: 1500

    function show() {
        shown = true
        hideTimer.restart()
    }

    Accessible.role: Accessible.StaticText
    Accessible.name: qsTr("Copied")

    color: Theme.color.green
    radius: 4
    opacity: shown ? 1 : 0
    visible: opacity > 0
    implicitWidth: contentRow.implicitWidth + 16
    implicitHeight: contentRow.implicitHeight + 8

    Behavior on opacity {
        NumberAnimation { duration: 150 }
    }

    Timer {
        id: hideTimer
        interval: root.visibleDurationMs
        onTriggered: root.shown = false
    }

    RowLayout {
        id: contentRow
        anchors.centerIn: parent
        spacing: 4

        Icon {
            source: "image://images/check"
            color: Theme.color.neutral0
            size: 14
        }

        CoreText {
            text: qsTr("Copied")
            font.pixelSize: 13
            color: Theme.color.neutral0
        }
    }
}

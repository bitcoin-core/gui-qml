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
    property string text: qsTr("Copied")
    property color toastColor: Theme.color.green
    property color contentColor: Theme.color.neutral0
    property url iconSource: "image://images/check"

    function show() {
        shown = true
        hideTimer.restart()
    }

    color: toastColor
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
            source: root.iconSource
            color: root.contentColor
            size: 14
        }

        CoreText {
            text: root.text
            font.pixelSize: 13
            color: root.contentColor
        }
    }
}

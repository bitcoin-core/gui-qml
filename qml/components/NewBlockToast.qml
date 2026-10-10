// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15

import "../controls"

// Short notice when a synced node accepts a new block.
Item {
    id: root
    objectName: "newBlockToast"

    property NodeStatus status: NodeStatus {}
    // Toasts appear only while the page showing them is on screen.
    property bool active: true
    property int duration: 3000
    property int blockHeight: 0
    readonly property bool shown: hideTimer.running
    // Assigned, not bound: a binding could update before the change handler
    // runs and hide the new tip.
    property int lastHeight: 0
    Component.onCompleted: root.lastHeight = root.status.nodeModelRef !== null ? root.status.nodeModelRef.blockTipHeight : 0

    implicitWidth: pill.implicitWidth
    implicitHeight: pill.implicitHeight
    visible: opacity > 0
    opacity: root.shown ? 1 : 0

    Behavior on opacity {
        NumberAnimation { duration: 300; easing.type: Easing.OutCubic }
    }

    Connections {
        target: root.status.nodeModelRef
        ignoreUnknownSignals: true
        function onBlockTipHeightChanged() {
            const height = root.status.nodeModelRef.blockTipHeight
            const isNewTip = height > root.lastHeight && root.lastHeight > 0
            root.lastHeight = height
            if (!isNewTip || !root.active || root.status.key !== "synced") return
            root.blockHeight = height
            hideTimer.restart()
        }
    }

    Timer {
        id: hideTimer
        interval: root.duration
    }

    Rectangle {
        id: pill
        implicitWidth: row.implicitWidth + 28
        implicitHeight: 36
        radius: 18
        color: Theme.color.neutral2
        border.width: 1
        border.color: Theme.color.neutral3
        // Rise into place as it fades in.
        y: root.shown ? 0 : 8

        Behavior on y {
            NumberAnimation { duration: 300; easing.type: Easing.OutCubic }
        }

        Row {
            id: row
            anchors.centerIn: parent
            spacing: 8
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 8
                height: 8
                radius: 4
                color: Theme.color.orange
            }
            CoreText {
                objectName: "newBlockToastText"
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("New block %1").arg(root.status.formatHeight(root.blockHeight))
                bold: true
                font.pixelSize: 13
                wrap: false
            }
        }
    }
}

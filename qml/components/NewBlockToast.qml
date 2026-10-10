// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15

import "../controls"

Item {
    id: root
    objectName: "newBlockToast"

    property NodeStatus status: NodeStatus {}
    property bool active: true
    property int duration: 3000
    property int blockHeight: 0
    readonly property bool shown: hideTimer.running
    // Assigned, not bound: a binding could update before the handler runs.
    property int lastHeight: 0
    Component.onCompleted: root.lastHeight = root.status.nodeModelRef !== null ? root.status.nodeModelRef.blockTipHeight : 0

    implicitWidth: pill.implicitWidth
    implicitHeight: pill.implicitHeight
    visible: opacity > 0
    opacity: root.shown ? 1 : 0

    Behavior on opacity {
        enabled: Theme.motion.animated
        NumberAnimation { duration: Theme.motion.fade(Theme.motion.slow); easing.type: Theme.motion.easing.enter }
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
        implicitWidth: row.implicitWidth + 2 * Theme.space.l
        implicitHeight: Math.max(Theme.size.control, row.implicitHeight + 2 * Theme.space.s)
        radius: height / 2
        color: Theme.color.surface2
        border.width: 1
        border.color: Theme.color.surface3
        y: root.shown || Theme.motion.reduced ? 0 : Theme.space.m

        Behavior on y {
            enabled: Theme.motion.animated
            NumberAnimation { duration: Theme.motion.travel(Theme.motion.slow); easing.type: Theme.motion.easing.enter }
        }

        Row {
            id: row
            anchors.centerIn: parent
            spacing: Theme.space.m
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: Theme.space.m
                height: Theme.space.m
                radius: height / 2
                color: Theme.color.accent
            }
            CoreText {
                objectName: "newBlockToastText"
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("New block %1").arg(root.status.formatHeight(root.blockHeight))
                textStyle: Theme.text.captionStrong
                wrap: false
            }
        }
    }
}

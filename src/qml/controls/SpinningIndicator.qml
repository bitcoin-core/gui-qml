// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15

Item {
    id: root

    property color color: Theme.color.neutral9
    property bool running: false
    property int strokeWidth: 2

    implicitWidth: 18
    implicitHeight: 18

    Canvas {
        id: arc
        anchors.fill: parent
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            ctx.strokeStyle = root.color
            ctx.lineWidth = root.strokeWidth
            ctx.lineCap = "round"
            ctx.beginPath()
            ctx.arc(width / 2, height / 2, width / 2 - root.strokeWidth, 0, Math.PI * 1.5)
            ctx.stroke()
        }
        Connections {
            target: root
            function onColorChanged() { arc.requestPaint() }
        }
    }

    RotationAnimator {
        target: root
        from: 0
        to: 360
        duration: 800
        loops: Animation.Infinite
        running: root.running
    }
}

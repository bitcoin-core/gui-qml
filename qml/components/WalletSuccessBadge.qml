// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import "../controls"

Item {
    id: root
    implicitWidth: 72
    implicitHeight: 72
    opacity: 0

    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: "transparent"
        border.width: 2
        border.color: Theme.color.green
        readonly property real pulse: Math.max(0, (checkCanvas.progress - 0.55) / 0.45)
        scale: 1 + pulse * 0.28
        opacity: Math.sin(pulse * Math.PI) * 0.35
    }

    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: Theme.color.green
    }
    Canvas {
        id: checkCanvas
        anchors.centerIn: parent
        width: 40
        height: 40
        antialiasing: true
        property real progress: 0
        onProgressChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            const points = [[5, 21], [16, 31], [35, 9]]
            const first = Math.hypot(points[1][0] - points[0][0], points[1][1] - points[0][1])
            const second = Math.hypot(points[2][0] - points[1][0], points[2][1] - points[1][1])
            // Let the ring recede as the check is drawn, keeping both strokes
            // legible throughout rather than collapsing the circle to a point.
            ctx.beginPath()
            ctx.arc(20, 20, 16, -Math.PI / 2 + progress * Math.PI * 2, Math.PI * 1.5)
            ctx.strokeStyle = Theme.color.white
            ctx.lineWidth = 3
            ctx.lineCap = "round"
            ctx.globalAlpha = 1 - progress
            ctx.stroke()
            ctx.globalAlpha = 1
            let remaining = progress * (first + second)
            ctx.beginPath()
            ctx.moveTo(points[0][0], points[0][1])
            for (let i = 0; i < 2 && remaining > 0; ++i) {
                const start = points[i]
                const end = points[i + 1]
                const length = i === 0 ? first : second
                const fraction = Math.min(1, remaining / length)
                ctx.lineTo(start[0] + (end[0] - start[0]) * fraction,
                           start[1] + (end[1] - start[1]) * fraction)
                remaining -= length
            }
            ctx.strokeStyle = Theme.color.white
            ctx.lineWidth = 5
            ctx.lineCap = "round"
            ctx.lineJoin = "round"
            ctx.stroke()
        }
    }

    SequentialAnimation {
        running: true
        NumberAnimation { target: root; property: "opacity"; from: 0; to: 1; duration: 280; easing.type: Easing.OutCubic }
        NumberAnimation { target: checkCanvas; property: "progress"; from: 0; to: 1; duration: 400; easing.type: Easing.OutCubic }
    }
}

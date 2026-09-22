// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15 as Controls
import org.bitcoincore.qt 1.0

Controls.CheckBox {
    id: root

    property color checkedColor: Theme.color.orange
    property color borderColor: Theme.color.neutral4
    property color checkColor: Theme.color.white
    property bool animationsEnabled: true

    padding: 0
    spacing: 10
    implicitHeight: 28
    focusPolicy: Qt.StrongFocus
    hoverEnabled: AppMode.isDesktop

    indicator: Rectangle {
        id: indicator
        property real checkProgress: root.checked ? 1 : 0

        implicitWidth: 20
        implicitHeight: 20
        y: (root.height - height) / 2
        radius: 6
        color: root.checked ? root.checkedColor : "transparent"
        border.width: root.visualFocus || !root.checked ? 2 : 0
        border.color: root.visualFocus ? Theme.color.orange : root.borderColor

        Behavior on color {
            enabled: root.animationsEnabled
            ColorAnimation { duration: 150; easing.type: Easing.OutCubic }
        }

        Behavior on checkProgress {
            enabled: root.animationsEnabled && root.checked
            NumberAnimation {
                duration: 350
                easing.type: Easing.OutCubic
            }
        }

        Canvas {
            id: checkCanvas
            anchors.centerIn: parent
            width: 14
            height: 14
            antialiasing: true

            readonly property real progress: indicator.checkProgress
            readonly property color strokeColor: root.checkColor
            onProgressChanged: requestPaint()
            onStrokeColorChanged: requestPaint()
            onPaint: {
                const ctx = getContext("2d")
                ctx.clearRect(0, 0, width, height)
                if (progress <= 0) return

                // Draw the two check segments by traveled path length.
                const points = [[1.2, 7.5], [5.0, 11.8], [12.8, 2.2]]
                const firstLength = Math.hypot(points[1][0] - points[0][0], points[1][1] - points[0][1])
                const secondLength = Math.hypot(points[2][0] - points[1][0], points[2][1] - points[1][1])
                let remaining = progress * (firstLength + secondLength)

                ctx.beginPath()
                ctx.moveTo(points[0][0], points[0][1])
                for (let i = 0; i < 2 && remaining > 0; ++i) {
                    const start = points[i]
                    const end = points[i + 1]
                    const length = i === 0 ? firstLength : secondLength
                    const fraction = Math.min(1, remaining / length)
                    ctx.lineTo(start[0] + (end[0] - start[0]) * fraction,
                               start[1] + (end[1] - start[1]) * fraction)
                    remaining -= length
                }
                ctx.strokeStyle = strokeColor
                ctx.lineWidth = 2
                ctx.lineCap = "round"
                ctx.lineJoin = "round"
                ctx.stroke()
            }
        }
    }

    contentItem: CoreText {
        text: root.text
        leftPadding: root.indicator.width + root.spacing
        verticalAlignment: Text.AlignVCenter
        horizontalAlignment: Text.AlignLeft
        font: Theme.text.description.font
        color: root.enabled ? Theme.color.neutral7 : Theme.color.neutral5
        wrapMode: Text.WordWrap
    }
}

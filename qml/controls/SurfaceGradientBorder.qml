// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15

Item {
    id: root

    property color surfaceColor: Theme.color.neutral1
    property color referenceColor: surfaceColor
    property var colors: Qt.colorEqual(referenceColor, Theme.color.neutral2)
        ? Theme.color.neutral2SurfaceBorderGradient : Theme.color.neutral1SurfaceBorderGradient
    property color topColor: colors[0]
    property color bottomColor: colors[1]
    property real cornerRadius: 10

    function shiftedColor(borderColor) {
        return Qt.rgba(
            Math.max(0, Math.min(1, surfaceColor.r + borderColor.r - referenceColor.r)),
            Math.max(0, Math.min(1, surfaceColor.g + borderColor.g - referenceColor.g)),
            Math.max(0, Math.min(1, surfaceColor.b + borderColor.b - referenceColor.b)),
            surfaceColor.a)
    }

    Rectangle {
        anchors.fill: parent
        radius: root.cornerRadius
        antialiasing: true
        gradient: Gradient {
            GradientStop { position: 0; color: root.shiftedColor(root.topColor) }
            GradientStop { position: 1; color: root.shiftedColor(root.bottomColor) }
        }

        Rectangle {
            anchors.fill: parent
            anchors.margins: 1
            radius: Math.max(0, root.cornerRadius - 1)
            antialiasing: true
            color: root.surfaceColor
        }
    }
}

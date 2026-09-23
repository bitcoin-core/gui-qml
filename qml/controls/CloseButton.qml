// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import org.bitcoincore.qt 1.0

Button {
    id: root

    property int size: 30
    property int iconSize: 10
    property color iconColor: Theme.color.neutral6
    property color backgroundColor: Theme.color.neutral2
    property color backgroundHoverColor: Theme.color.neutral3
    property color backgroundPressedColor: Theme.color.neutral3

    implicitWidth: size
    implicitHeight: size
    hoverEnabled: enabled && AppMode.isDesktop
    focusPolicy: Qt.StrongFocus
    padding: 0
    scale: enabled && down ? 0.95 : 1.0
    Accessible.name: qsTr("Close")
    Accessible.role: Accessible.Button

    Behavior on scale {
        NumberAnimation {
            duration: 100
            easing.type: Easing.OutCubic
        }
    }

    HoverHandler {
        cursorShape: Qt.PointingHandCursor
    }

    contentItem: Icon {
        objectName: root.objectName.length > 0 ? root.objectName + "Icon" : ""
        anchors.centerIn: parent
        source: "image://images/cross"
        color: root.iconColor
        size: root.iconSize
        hoverEnabled: false
    }

    background: Rectangle {
        id: background
        objectName: root.objectName.length > 0 ? root.objectName + "Background" : ""
        radius: width / 2
        color: root.down
            ? root.backgroundPressedColor
            : root.hovered
                ? root.backgroundHoverColor
                : root.backgroundColor

        Behavior on color {
            ColorAnimation { duration: 150 }
        }

        SurfaceGradientBorder {
            anchors.fill: parent
            surfaceColor: background.color
            referenceColor: root.backgroundColor
            cornerRadius: background.radius
        }

        FocusBorder {
            visible: root.enabled && root.visualFocus
            borderRadius: root.size / 2 + 4
        }
    }
}

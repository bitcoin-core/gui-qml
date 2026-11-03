// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import org.bitcoincore.qt 1.0

Button {
    id: root

    // Medium is the close button of every modal and banner. Large is for a
    // full-window flow's navigation bar, where 30 px reads too small.
    enum Size { Medium, Large }

    property int buttonSize: CloseButton.Medium
    property int size: buttonSize === CloseButton.Large ? Theme.size.iconButton : 30
    property int iconSize: buttonSize === CloseButton.Large ? Theme.size.icon.s : 10
    property int backgroundRadius: size / 2
    property color iconColor: Theme.color.neutral6
    property color backgroundColor: Theme.color.surface2
    property color backgroundHoverColor: Theme.color.surface3
    property color backgroundPressedColor: Theme.color.surface3

    implicitWidth: size
    implicitHeight: size
    hoverEnabled: enabled && AppMode.isDesktop
    focusPolicy: Qt.StrongFocus
    padding: 0
    scale: enabled && down ? Theme.motion.pressScale : 1
    Accessible.name: qsTr("Close")
    Accessible.role: Accessible.Button

    Behavior on scale {
        enabled: Theme.motion.animated
        NumberAnimation {
            duration: Theme.motion.fast
            easing.type: Theme.motion.easing.enter
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
        radius: root.backgroundRadius
        color: root.down
            ? root.backgroundPressedColor
            : root.hovered
                ? root.backgroundHoverColor
                : root.backgroundColor

        Behavior on color {
            enabled: Theme.motion.animated
            ColorAnimation { duration: Theme.motion.normal }
        }

        SurfaceGradientBorder {
            anchors.fill: parent
            surfaceColor: background.color
            referenceColor: root.backgroundColor
            cornerRadius: background.radius
        }

        FocusBorder {
            visible: root.enabled && root.visualFocus
            targetRadius: root.backgroundRadius
        }
    }
}

// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

Button {
    id: root
    hoverEnabled: enabled && AppMode.isDesktop
    opacity: enabled ? 1.0 : 0.4
    scale: enabled && down ? Theme.motion.pressScale : 1

    Behavior on scale {
        enabled: Theme.motion.animated
        NumberAnimation {
            duration: Theme.motion.fast
            easing.type: Theme.motion.easing.enter
        }
    }

    // Fills with danger instead of accent, for actions that destroy or cannot be undone.
    property bool destructive: false
    property color textColor: Theme.color.accentForeground
    property color textHoverColor: textColor
    property color textPressedColor: textColor
    property color backgroundColor: destructive ? Theme.color.danger : Theme.color.accent
    property color backgroundHoverColor: destructive ? Theme.color.dangerHover : Theme.color.accentHover
    property color backgroundPressedColor: destructive ? Theme.color.dangerPressed : Theme.color.accentPressed
    property color borderColor: "transparent"
    property color borderHoverColor: "transparent"
    property color borderPressedColor: "transparent"
    property bool bold: true
    property bool busy: false
    property int backgroundRadius: Theme.radius.s
    property url iconSource: ""
    property var textStyle: bold ? Theme.text.buttonStrong : Theme.text.button

    contentItem: Item {
        implicitWidth: contentRow.implicitWidth
        implicitHeight: contentRow.implicitHeight

        RowLayout {
            id: contentRow
            anchors.centerIn: parent
            width: Math.min(implicitWidth, parent.width)
            spacing: Theme.space.xs

            SpinningIndicator {
                Layout.alignment: Qt.AlignVCenter
                visible: root.busy
                running: root.busy
                color: root.textColor
            }

            Icon {
                Layout.alignment: Qt.AlignVCenter
                visible: !root.busy && root.iconSource.toString() !== ""
                source: root.iconSource
                color: root.textColor
                size: Theme.size.icon.xl
            }

            CoreText {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                wrap: false
                elide: Text.ElideRight
                Layout.alignment: Qt.AlignVCenter
                text: root.text
                color: root.textColor
                textStyle: root.textStyle
            }
        }
    }
    background: Rectangle {
        id: bg
        implicitHeight: Theme.size.controlLarge
        color: backgroundColor
        border.color: borderColor
        radius: root.backgroundRadius

        states: [
            State {
                name: "PRESSED"; when: root.enabled && root.down
                PropertyChanges { target: bg; color: backgroundPressedColor }
                PropertyChanges { target: bg; border.color: borderPressedColor }
                PropertyChanges { target: root; textColor: textPressedColor }
            },
            State {
                name: "HOVER"; when: root.enabled && root.hovered
                PropertyChanges { target: bg; color: backgroundHoverColor }
                PropertyChanges { target: bg; border.color: borderHoverColor }
                PropertyChanges { target: root; textColor: textHoverColor }
            }
        ]

        Behavior on color {
            enabled: Theme.motion.animated
            ColorAnimation { duration: Theme.motion.normal }
        }

        SurfaceGradientBorder {
            anchors.fill: parent
            visible: (Qt.colorEqual(root.backgroundColor, Theme.color.accent)
                || Qt.colorEqual(root.backgroundColor, Theme.color.danger))
                && Qt.colorEqual(bg.border.color, "transparent")
            surfaceColor: bg.color
            referenceColor: Qt.colorEqual(root.backgroundColor, Theme.color.danger)
                ? Theme.color.danger : Theme.color.accent
            colors: Qt.colorEqual(root.backgroundColor, Theme.color.danger)
                ? Theme.color.dangerBorderGradient
                : Theme.color.accentBorderGradient
            cornerRadius: bg.radius
        }

        FocusBorder {
            visible: root.enabled && root.visualFocus
        }
    }
}

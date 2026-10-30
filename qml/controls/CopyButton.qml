// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import org.bitcoincore.qt 1.0

AbstractButton {
    id: root

    enum DisplayMode {
        IconOnly,
        TextOnly,
        IconAndText
    }

    property int displayMode: CopyButton.IconAndText
    property string copyText: qsTr("Copy")
    property string copiedText: qsTr("Copied")
    property url copyIconSource: "qrc:/icons/copy"
    property url copiedIconSource: "qrc:/icons/check"
    property int iconSize: Theme.size.icon.m
    property int resetInterval: 1000
    readonly property bool copied: copiedResetTimer.running

    readonly property bool _showsIcon: displayMode !== CopyButton.TextOnly
    readonly property bool _showsText: displayMode !== CopyButton.IconOnly
    readonly property color _iconColor: !enabled
        ? Theme.color.disabledContent
        : hovered || down ? Theme.color.neutral9 : Theme.color.neutral7
    readonly property color _textColor: !enabled
        ? Theme.color.disabledContent
        : hovered || down ? Theme.color.neutral9 : Theme.color.neutral8
    // The copied swap fades, and also scales unless motion is reduced.
    readonly property real _swapScale: Theme.motion.reduced ? 1 : 0.9

    signal copyRequested()

    text: copied ? copiedText : copyText
    Accessible.name: text
    hoverEnabled: AppMode.isDesktop
    focusPolicy: Qt.StrongFocus
    leftPadding: Theme.space.s
    rightPadding: Theme.space.s
    topPadding: 0
    bottomPadding: 0
    implicitHeight: Theme.size.controlSmall
    scale: enabled && down ? Theme.motion.pressScale : 1

    onClicked: {
        copiedResetTimer.restart()
        root.copyRequested()
    }

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

    contentItem: Item {
        implicitWidth: copyContent.implicitWidth
        implicitHeight: copyContent.implicitHeight

        Row {
            id: copyContent
            objectName: root.objectName.length > 0 ? root.objectName + "Content" : ""
            anchors.centerIn: parent
            spacing: 0

            Item {
                id: iconContainer
                objectName: root.objectName.length > 0 ? root.objectName + "IconContainer" : ""
                visible: root._showsIcon
                width: visible ? root.iconSize + 2 : 0
                height: visible ? root.iconSize + 2 : 0

                Icon {
                    objectName: root.objectName.length > 0 ? root.objectName + "CopyIcon" : ""
                    anchors.centerIn: parent
                    source: root.copyIconSource
                    color: root._iconColor
                    size: root.iconSize
                    opacity: root.copied ? 0 : 1
                    scale: root.copied ? root._swapScale : 1
                    hoverEnabled: false

                    Behavior on opacity { enabled: Theme.motion.animated; NumberAnimation { duration: Theme.motion.normal; easing.type: Theme.motion.easing.enter } }
                    Behavior on scale { enabled: Theme.motion.animated; NumberAnimation { duration: Theme.motion.normal; easing.type: Theme.motion.easing.enter } }
                }

                Icon {
                    objectName: root.objectName.length > 0 ? root.objectName + "CopiedIcon" : ""
                    anchors.centerIn: parent
                    source: root.copiedIconSource
                    color: root._iconColor
                    size: root.iconSize
                    opacity: root.copied ? 1 : 0
                    scale: root.copied ? 1 : root._swapScale
                    hoverEnabled: false

                    Behavior on opacity { enabled: Theme.motion.animated; NumberAnimation { duration: Theme.motion.normal; easing.type: Theme.motion.easing.enter } }
                    Behavior on scale { enabled: Theme.motion.animated; NumberAnimation { duration: Theme.motion.normal; easing.type: Theme.motion.easing.enter } }
                }
            }

            Item {
                id: textContainer
                objectName: root.objectName.length > 0 ? root.objectName + "TextContainer" : ""
                visible: root._showsText
                width: visible ? Math.max(copyLabel.implicitWidth, copiedLabel.implicitWidth) : 0
                height: visible ? Math.max(copyLabel.implicitHeight, copiedLabel.implicitHeight) : 0

                CoreText {
                    id: copyLabel
                    objectName: root.objectName.length > 0 ? root.objectName + "CopyText" : ""
                    anchors.centerIn: parent
                    text: root.copyText
                    color: root._textColor
                    textStyle: Theme.text.caption
                    opacity: root.copied ? 0 : 1
                    scale: root.copied ? root._swapScale : 1

                    Behavior on opacity { enabled: Theme.motion.animated; NumberAnimation { duration: Theme.motion.normal; easing.type: Theme.motion.easing.enter } }
                    Behavior on scale { enabled: Theme.motion.animated; NumberAnimation { duration: Theme.motion.normal; easing.type: Theme.motion.easing.enter } }
                }

                CoreText {
                    id: copiedLabel
                    objectName: root.objectName.length > 0 ? root.objectName + "CopiedText" : ""
                    anchors.centerIn: parent
                    text: root.copiedText
                    color: root._textColor
                    textStyle: Theme.text.caption
                    opacity: root.copied ? 1 : 0
                    scale: root.copied ? 1 : root._swapScale

                    Behavior on opacity { enabled: Theme.motion.animated; NumberAnimation { duration: Theme.motion.normal; easing.type: Theme.motion.easing.enter } }
                    Behavior on scale { enabled: Theme.motion.animated; NumberAnimation { duration: Theme.motion.normal; easing.type: Theme.motion.easing.enter } }
                }
            }
        }
    }

    background: StateBackground {
        stateColor: root.down ? Theme.color.surfacePressed
            : root.hovered ? Theme.color.surfaceHover : "transparent"
        radius: Theme.radius.m

        FocusBorder {
            visible: root.visualFocus
            topMargin: -Theme.focusRing.offsetDense
            bottomMargin: -Theme.focusRing.offsetDense
            leftMargin: -Theme.focusRing.offsetDense
            rightMargin: -Theme.focusRing.offsetDense
            borderRadius: 9
        }
    }

    Timer {
        id: copiedResetTimer
        interval: root.resetInterval
    }
}

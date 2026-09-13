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
    property int iconSize: 16
    property int resetInterval: 1000
    readonly property bool copied: copiedResetTimer.running

    readonly property bool _showsIcon: displayMode !== CopyButton.TextOnly
    readonly property bool _showsText: displayMode !== CopyButton.IconOnly
    readonly property color _iconColor: !enabled
        ? Theme.color.neutral4
        : hovered || down ? Theme.color.orange : Theme.color.neutral7
    readonly property color _textColor: !enabled
        ? Theme.color.neutral4
        : hovered || down ? Theme.color.orange : Theme.color.neutral8

    signal copyRequested()

    text: copied ? copiedText : copyText
    Accessible.name: text
    hoverEnabled: AppMode.isDesktop
    focusPolicy: Qt.StrongFocus
    leftPadding: 6
    rightPadding: 6
    topPadding: 0
    bottomPadding: 0
    implicitHeight: 28
    scale: enabled && down ? 0.98 : 1.0

    onClicked: {
        copiedResetTimer.restart()
        root.copyRequested()
    }

    Behavior on scale {
        NumberAnimation {
            duration: 100
            easing.type: Easing.OutCubic
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
                    scale: root.copied ? 0.75 : 1
                    hoverEnabled: false

                    Behavior on opacity { NumberAnimation { duration: 150 } }
                    Behavior on scale { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
                }

                Icon {
                    objectName: root.objectName.length > 0 ? root.objectName + "CopiedIcon" : ""
                    anchors.centerIn: parent
                    source: root.copiedIconSource
                    color: root._iconColor
                    size: root.iconSize
                    opacity: root.copied ? 1 : 0
                    scale: root.copied ? 1 : 0.75
                    hoverEnabled: false

                    Behavior on opacity { NumberAnimation { duration: 150 } }
                    Behavior on scale { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
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
                    font: Theme.text.caption.font
                    lineHeight: Theme.text.caption.lineHeight
                    lineHeightMode: Text.FixedHeight
                    opacity: root.copied ? 0 : 1
                    scale: root.copied ? 0.9 : 1

                    Behavior on opacity { NumberAnimation { duration: 150 } }
                    Behavior on scale { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
                }

                CoreText {
                    id: copiedLabel
                    objectName: root.objectName.length > 0 ? root.objectName + "CopiedText" : ""
                    anchors.centerIn: parent
                    text: root.copiedText
                    color: root._textColor
                    font: Theme.text.caption.font
                    lineHeight: Theme.text.caption.lineHeight
                    lineHeightMode: Text.FixedHeight
                    opacity: root.copied ? 1 : 0
                    scale: root.copied ? 1 : 0.9

                    Behavior on opacity { NumberAnimation { duration: 150 } }
                    Behavior on scale { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
                }
            }
        }
    }

    background: Rectangle {
        color: root.hovered || root.down ? Theme.color.neutral3 : "transparent"
        radius: 7

        Behavior on color {
            ColorAnimation { duration: 150 }
        }

        FocusBorder {
            visible: root.visualFocus
            topMargin: -2
            bottomMargin: -2
            leftMargin: -2
            rightMargin: -2
            borderRadius: 9
        }
    }

    Timer {
        id: copiedResetTimer
        interval: root.resetInterval
    }
}

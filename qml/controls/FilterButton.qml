// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

AbstractButton {
    id: root

    property bool active: false
    property int count: 0
    property color inactiveIconColor: Theme.color.neutral6
    property color activeIconColor: Theme.color.white
    property color activeBackgroundColor: Theme.color.orange
    property color backgroundColor: Theme.color.neutral1
    property color hoverBackgroundColor: Theme.color.neutral2
    property int size: 36
    property int iconSize: 24
    property int transitionDuration: 140
    property real minimizedIconScale: 0.72
    readonly property Item iconItem: active ? activeFilterIcon : inactiveFilterIcon
    readonly property alias inactiveIconItem: inactiveFilterIcon
    readonly property alias activeIconItem: activeFilterIcon

    implicitWidth: size + (count > 0 ? countLabel.implicitWidth + 6 : 0)
    implicitHeight: size
    width: implicitWidth
    height: size
    padding: 0
    hoverEnabled: AppMode.isDesktop
    focusPolicy: Qt.StrongFocus

    Accessible.role: Accessible.Button
    Accessible.name: text.length > 0 ? text : qsTr("Filter")
    Accessible.checkable: true
    Accessible.checked: active

    HoverHandler {
        cursorShape: Qt.PointingHandCursor
    }

    background: Rectangle {
        color: root.active ? root.activeBackgroundColor : root.hovered || root.down ? root.hoverBackgroundColor : root.backgroundColor
        radius: 6

        FocusBorder {
            objectName: root.objectName.length > 0 ? root.objectName + "FocusBorder" : ""
            visible: root.visualFocus
            borderRadius: 9
        }
    }

    function paintIcon(canvas) {
        const ctx = canvas.getContext("2d")
        ctx.clearRect(0, 0, canvas.width, canvas.height)
        ctx.save()
        ctx.scale(canvas.width / 128, canvas.height / 128)
        ctx.strokeStyle = canvas.color
        ctx.lineWidth = 9
        ctx.lineCap = "round"
        const lines = [[15, 113, 32], [26, 102, 64], [37, 91, 96]]
        for (let i = 0; i < lines.length; ++i) {
            ctx.beginPath()
            ctx.moveTo(lines[i][0], lines[i][2])
            ctx.lineTo(lines[i][1], lines[i][2])
            ctx.stroke()
        }
        ctx.restore()
    }

    contentItem: RowLayout {
        spacing: 6
        Item {
            Layout.leftMargin: (root.size - root.iconSize) / 2
            Layout.preferredWidth: root.iconSize
            Layout.preferredHeight: root.iconSize

            Canvas {
                onPaint: root.paintIcon(inactiveFilterIcon)
                id: inactiveFilterIcon
                objectName: root.objectName.length > 0 ? root.objectName + "InactiveIcon" : ""
                anchors.centerIn: parent
                property color color: root.enabled
                    ? root.hovered || root.down
                        ? Theme.color.orange
                        : root.inactiveIconColor
                    : Theme.color.neutral4
                property int size: root.iconSize
                width: size
                height: size
                onColorChanged: requestPaint()
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()
                opacity: root.active ? 0 : 1
                scale: root.active ? root.minimizedIconScale : 1

                Behavior on color {
                    ColorAnimation { duration: 150 }
                }
                Behavior on opacity {
                    NumberAnimation {
                        duration: root.transitionDuration
                        easing.type: Easing.OutCubic
                    }
                }
                Behavior on scale {
                    NumberAnimation {
                        duration: root.transitionDuration
                        easing.type: Easing.OutCubic
                    }
                }
            }

            Canvas {
                onPaint: root.paintIcon(activeFilterIcon)
                id: activeFilterIcon
                objectName: root.objectName.length > 0 ? root.objectName + "ActiveIcon" : ""
                anchors.centerIn: parent
                property color color: root.enabled ? root.activeIconColor : Theme.color.neutral4
                property int size: root.iconSize
                width: size
                height: size
                onColorChanged: requestPaint()
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()
                opacity: root.active ? 1 : 0
                scale: root.active ? 1 : root.minimizedIconScale

                Behavior on color {
                    ColorAnimation { duration: 150 }
                }
                Behavior on opacity {
                    NumberAnimation {
                        duration: root.transitionDuration
                        easing.type: Easing.OutCubic
                    }
                }
                Behavior on scale {
                    NumberAnimation {
                        duration: root.transitionDuration
                        easing.type: Easing.OutCubic
                    }
                }
            }
        }
        CoreText {
            id: countLabel
            objectName: root.objectName.length > 0 ? root.objectName + "Count" : ""
            visible: root.count > 0
            Layout.rightMargin: (root.size - root.iconSize) / 2
            text: root.count
            font: Theme.text.description.font
            color: root.active ? root.activeIconColor : root.inactiveIconColor
        }
        Item { Layout.fillWidth: true; visible: root.count === 0 }
    }
}

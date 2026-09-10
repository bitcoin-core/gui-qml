// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import org.bitcoincore.qt 1.0

AbstractButton {
    id: root

    property bool active: false
    property url inactiveIconSource: "qrc:/icons/filter"
    property url activeIconSource: "qrc:/icons/filter-active"
    property color inactiveIconColor: Theme.color.neutral6
    property color activeIconColor: Theme.color.orange
    property color backgroundColor: Theme.color.neutral1
    property color hoverBackgroundColor: Theme.color.neutral2
    property int size: 36
    property int iconSize: 24
    property int transitionDuration: 140
    property real minimizedIconScale: 0.72
    readonly property Item iconItem: active ? activeFilterIcon : inactiveFilterIcon
    readonly property alias inactiveIconItem: inactiveFilterIcon
    readonly property alias activeIconItem: activeFilterIcon

    implicitWidth: size
    implicitHeight: size
    width: size
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
        color: root.hovered || root.down ? root.hoverBackgroundColor : root.backgroundColor
        radius: 5

        FocusBorder {
            visible: root.visualFocus
            borderRadius: 9
        }
    }

    contentItem: Item {
        Icon {
            id: inactiveFilterIcon
            objectName: root.objectName.length > 0 ? root.objectName + "InactiveIcon" : ""
            anchors.centerIn: parent
            source: root.inactiveIconSource
            color: root.enabled
                ? root.hovered || root.down
                    ? root.activeIconColor
                    : root.inactiveIconColor
                : Theme.color.neutral4
            size: root.iconSize
            opacity: root.active ? 0 : 1
            scale: root.active ? root.minimizedIconScale : 1
            hoverEnabled: false

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

        Icon {
            id: activeFilterIcon
            objectName: root.objectName.length > 0 ? root.objectName + "ActiveIcon" : ""
            anchors.centerIn: parent
            source: root.activeIconSource
            color: root.enabled ? root.activeIconColor : Theme.color.neutral4
            size: root.iconSize
            opacity: root.active ? 1 : 0
            scale: root.active ? 1 : root.minimizedIconScale
            hoverEnabled: false

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
}

// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import org.bitcoincore.qt 1.0

AbstractButton {
    id: root

    property url iconSource: "image://images/ellipsis"
    property color iconColor: Theme.color.neutral5
    property color activeIconColor: Theme.color.orange
    property color backgroundColor: Theme.color.neutral1
    property color hoverBackgroundColor: Theme.color.neutral2
    property int size: 36
    property int iconSize: 30
    readonly property alias iconItem: ellipsisIcon

    implicitWidth: size
    implicitHeight: size
    width: size
    height: size
    padding: 0
    hoverEnabled: AppMode.isDesktop
    focusPolicy: Qt.StrongFocus

    Accessible.role: Accessible.Button
    Accessible.name: text.length > 0 ? text : qsTr("More options")

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
            id: ellipsisIcon
            objectName: root.objectName.length > 0 ? root.objectName + "Icon" : ""
            anchors.centerIn: parent
            source: root.iconSource
            color: root.enabled
                ? root.checked || root.hovered || root.down
                    ? root.activeIconColor
                    : root.iconColor
                : Theme.color.neutral4
            size: root.iconSize
            hoverEnabled: false

            Behavior on color {
                ColorAnimation { duration: 150 }
            }
        }
    }
}

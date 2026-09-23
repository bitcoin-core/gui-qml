// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import org.bitcoincore.qt 1.0

NeutralButton {
    id: root

    property bool isOnSurface: false
    property color iconColor: Theme.color.neutral7
    property color activeIconColor: Theme.color.orange
    property int size: 40
    property bool circular: false
    readonly property alias iconItem: ellipsisIcon

    iconSource: "image://images/ellipsis"
    iconSize: 40
    backgroundColor: isOnSurface ? Theme.color.neutral2 : Theme.color.neutral1
    hoverBackgroundColor: isOnSurface ? Theme.color.neutral3 : Theme.color.neutral2
    showBorder: isOnSurface
    backgroundRadius: circular ? size / 2 : 5
    scale: 1

    implicitWidth: size
    implicitHeight: size
    width: size
    height: size
    padding: 0
    hoverEnabled: AppMode.isDesktop
    focusPolicy: Qt.StrongFocus

    Accessible.role: Accessible.Button
    Accessible.name: text.length > 0 ? text : qsTr("More options")

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

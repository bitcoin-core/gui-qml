// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import org.bitcoincore.qt 1.0

NeutralButton {
    id: root
    property bool keyboardActivation: false
    Keys.onPressed: function(event) {
        keyboardActivation = event.key === Qt.Key_Space || event.key === Qt.Key_Down
        if (event.key === Qt.Key_Down) {
            root.clicked()
            event.accepted = true
        }
    }
    TapHandler { onPressedChanged: if (pressed) root.keyboardActivation = false }

    surfaceLevel: 0
    property color iconColor: Theme.color.neutral7
    property color activeIconColor: Theme.color.neutral9
    property int size: Theme.size.iconButton
    property bool circular: false
    readonly property alias iconItem: ellipsisIcon

    iconSource: "image://images/ellipsis"
    // Ellipsis artwork is drawn on a 24-unit grid; 20 suits every target size.
    iconSize: Theme.size.icon.l
    backgroundColor: Theme.color.surface(surfaceLevel + 1)
    hoverBackgroundColor: Theme.color.surface(surfaceLevel + 2)
    showBorder: surfaceLevel > 0
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
                : Theme.color.disabledContent
            size: root.iconSize
            hoverEnabled: false

            Behavior on color {
                enabled: Theme.motion.animated
                ColorAnimation { duration: Theme.motion.normal }
            }
        }
    }
}

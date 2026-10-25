// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import org.bitcoincore.qt 1.0

// A setting that applies immediately. The switch is the one focus
// stop and takes the row's label as its name; clicking anywhere on the row
// flips it. Bind `checked` and write the setting in onToggled.
FormRow {
    id: root

    property alias checked: toggle.checked
    property string switchObjectName: objectName.length > 0 ? objectName + "Switch" : ""
    readonly property alias switchItem: toggle
    property int cornerRadius: Theme.radius.listRow

    signal toggled()

    background: Item {
        StateBackground {
            anchors.fill: parent
            radius: root.cornerRadius
            stateColor: !root.enabled ? "transparent"
                : rowTap.pressed ? Theme.color.surfacePressed
                : rowHover.hovered ? Theme.color.surfaceHover
                : "transparent"
        }

        Rectangle {
            visible: root.showDivider
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.leftMargin: root.dividerLeftInset
            anchors.rightMargin: root.dividerRightInset
            height: 1
            color: root.dividerColor
        }

        HoverHandler {
            id: rowHover
            enabled: root.enabled && AppMode.isDesktop
            cursorShape: Qt.PointingHandCursor
        }

        // Presses on the switch itself never reach this handler.
        TapHandler {
            id: rowTap
            enabled: root.enabled
            onTapped: {
                toggle.toggle()
                toggle.toggled()
            }
        }
    }

    trailingItem: OptionSwitch {
        id: toggle
        objectName: root.switchObjectName
        onToggled: root.toggled()
    }
}

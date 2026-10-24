// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15

Switch {
    id: root

    // Set by a hosting FormRow, whose label names the switch.
    property string rowTitle: ""
    property string rowDescription: ""

    Accessible.name: text.length > 0 ? text : rowTitle
    Accessible.description: rowDescription
    implicitWidth: 45
    implicitHeight: 28
    focusPolicy: Qt.StrongFocus
    opacity: enabled ? 1.0 : 0.4
    // Fade the control as one layer, so the track does not show through the thumb.
    layer.enabled: !enabled
    background: Rectangle {
        radius: Math.floor(height / 2)
        color: root.checked ? Theme.color.orange : Theme.color.neutral4

        Behavior on color {
            ColorAnimation { duration: 150 }
        }
    }
    indicator: Rectangle {
        property real _margin: Math.round((parent.height - height) / 2)
        y: Math.round(parent.height - height) / 2
        x: root.checked ? parent.width - width - _margin : _margin
        radius: 10
        width: 20
        height: 20
        color: Theme.color.white
        Behavior on x {
            SmoothedAnimation {
            }
        }

        Behavior on color {
            ColorAnimation { duration: 150 }
        }
    }

    FocusBorder {
        objectName: root.objectName.length > 0 ? root.objectName + "FocusBorder" : ""
        visible: root.visualFocus
        targetRadius: Math.floor(root.height / 2)
        z: 1
    }
}

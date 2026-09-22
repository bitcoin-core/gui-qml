// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15 as Controls

Controls.Slider {
    id: root

    property alias minValue: root.from
    property alias maxValue: root.to
    property alias currentValue: root.value
    property bool discrete: false
    property color trackColor: Theme.color.neutral2
    property color trackHighlightColor: Theme.color.orange
    property color thumbColor: Theme.color.white

    implicitWidth: 260
    implicitHeight: 44
    padding: 0
    from: 0
    to: 100
    value: from
    stepSize: discrete ? 1 : 0
    snapMode: discrete ? Controls.Slider.SnapAlways : Controls.Slider.NoSnap
    live: true
    enabled: to > from

    handle: Rectangle {
        objectName: root.objectName.length > 0 ? root.objectName + "Handle" : "sliderHandle"
        x: root.leftPadding + root.visualPosition * (root.availableWidth - width)
        y: root.topPadding + (root.availableHeight - height) / 2
        implicitWidth: 28
        implicitHeight: 20
        radius: 10
        color: root.thumbColor
        border.width: 1
        border.color: "#E6E6E6"

        FocusBorder {
            visible: root.visualFocus && parent.activeFocus
            borderRadius: 14
        }
    }

    background: Rectangle {
        x: root.leftPadding + root.handle.width / 2
        y: root.topPadding + (root.availableHeight - height) / 2
        width: Math.max(0, root.availableWidth - root.handle.width)
        height: 6
        radius: 3
        color: root.trackColor

        Rectangle {
            width: root.visualPosition * parent.width
            height: parent.height
            radius: parent.radius
            color: root.trackHighlightColor
        }
    }
}

// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15 as Controls

Controls.RangeSlider {
    id: root

    property alias minValue: root.from
    property alias maxValue: root.to
    property alias lowerValue: root.first.value
    property alias upperValue: root.second.value
    property color trackColor: Theme.color.neutral2
    property color trackHighlightColor: Theme.color.orange
    property color thumbColor: Theme.color.white

    implicitWidth: 260
    implicitHeight: 44
    padding: 0
    from: 0
    to: 100
    first.value: from
    second.value: to
    stepSize: 0
    snapMode: Controls.RangeSlider.NoSnap
    live: true
    enabled: to > from

    first.handle: Thumb {
        objectName: "rangeSliderLowerHandle"
        x: root.leftPadding + root.first.visualPosition * (root.availableWidth - width)
        y: root.topPadding + (root.availableHeight - height) / 2
        Accessible.name: qsTr("Lower value")
    }
    second.handle: Thumb {
        objectName: "rangeSliderUpperHandle"
        x: root.leftPadding + root.second.visualPosition * (root.availableWidth - width)
        y: root.topPadding + (root.availableHeight - height) / 2
        Accessible.name: qsTr("Upper value")
    }

    background: Rectangle {
        x: root.leftPadding + root.first.handle.width / 2
        y: root.topPadding + (root.availableHeight - height) / 2
        width: Math.max(0, root.availableWidth - root.first.handle.width)
        height: 6
        radius: 3
        color: root.trackColor

        Rectangle {
            x: Math.min(root.first.visualPosition, root.second.visualPosition) * parent.width
            width: Math.abs(root.second.visualPosition - root.first.visualPosition) * parent.width
            height: parent.height
            radius: parent.radius
            color: root.trackHighlightColor
        }
    }

    component Thumb: Rectangle {
        implicitWidth: 28
        implicitHeight: 20
        radius: 10
        color: root.thumbColor
        border.width: 1
        border.color: "#E6E6E6"
        FocusBorder { visible: root.visualFocus && parent.activeFocus; borderRadius: 14 }
    }
}

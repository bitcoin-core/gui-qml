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
    property bool valuesHidden: false
    property color trackColor: Theme.color.sliderTrack
    property color trackHighlightColor: Theme.color.accent
    property color thumbColor: Theme.color.accentForeground

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
    opacity: enabled ? 1.0 : 0.4
    // Fade the control as one layer, so the track does not show through the thumb.
    layer.enabled: !enabled

    first.handle: Thumb {
        node: root.first
        range: root
        objectName: "rangeSliderLowerHandle"
        x: root.leftPadding + root.first.visualPosition * (root.availableWidth - width)
        y: root.topPadding + (root.availableHeight - height) / 2
        Accessible.name: qsTr("Lower value")
    }
    second.handle: Thumb {
        node: root.second
        range: root
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
        radius: Theme.radius.xs
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
        id: thumb
        required property var node
        required property var range
        // Read by the accessibility value interface of each handle.
        readonly property var value: thumb.range.valuesHidden ? undefined : node.value
        readonly property var minimumValue: thumb.range.valuesHidden ? undefined : thumb.range.from
        readonly property var maximumValue: thumb.range.valuesHidden ? undefined : thumb.range.to
        Accessible.description: thumb.range.valuesHidden ? qsTr("Hidden value") : ""

        Accessible.role: Accessible.Slider
        // Step as the arrow keys do, which also report the move.
        Accessible.onIncreaseAction: {
            node.increase()
            node.moved()
        }
        Accessible.onDecreaseAction: {
            node.decrease()
            node.moved()
        }
        implicitWidth: 28
        implicitHeight: 20
        radius: height / 2
        color: thumb.range.thumbColor
        border.width: 1
        border.color: Theme.color.controlBorder
        // Qt 6.5 and older report OtherFocusReason when Tab reaches a handle,
        // so visualFocus stays false there; hide the ring only after a press.
        property bool pointerFocus: false
        MouseArea {
            anchors.fill: parent
            onPressed: function(mouse) {
                thumb.pointerFocus = true
                mouse.accepted = false
            }
        }
        onActiveFocusChanged: if (!activeFocus) pointerFocus = false
        FocusBorder { visible: thumb.activeFocus && (thumb.range.visualFocus || !thumb.pointerFocus); borderRadius: 14 }
    }
}

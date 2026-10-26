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
    readonly property real minimumValue: from
    readonly property real maximumValue: to

    Keys.onPressed: function(event) {
        let next = value
        if (event.key === Qt.Key_Home) next = from
        else if (event.key === Qt.Key_End) next = to
        else if (event.key === Qt.Key_PageUp) next += Math.max(stepSize, (to - from) / 10)
        else if (event.key === Qt.Key_PageDown) next -= Math.max(stepSize, (to - from) / 10)
        else return
        if (discrete && stepSize > 0) next = from + Math.round((next - from) / stepSize) * stepSize
        value = Math.max(from, Math.min(to, next))
        moved()
        event.accepted = true
    }
    property bool discrete: false
    property color trackColor: Theme.color.sliderTrack
    property color trackHighlightColor: Theme.color.accent
    property color thumbColor: Theme.color.accentForeground
    // A dot under every step of a discrete slider.
    property bool showsTickMarks: false
    // Labels under chosen steps, as { value, text }.
    property var tickLabels: []
    readonly property int stepCount: discrete && stepSize > 0 ? Math.round((to - from) / stepSize) : 0
    // The track stays where it is when labels add height below it.
    readonly property real trackCenterY: topPadding + 22

    implicitWidth: 260
    implicitHeight: tickLabels.length > 0 ? 44 + Theme.text.caption.lineHeight : 44
    padding: 0
    from: 0
    to: 100
    value: from
    stepSize: discrete ? 1 : 0
    snapMode: discrete ? Controls.Slider.SnapAlways : Controls.Slider.NoSnap
    live: true
    enabled: to > from
    opacity: enabled ? 1.0 : 0.4
    // Fade the control as one layer, so the track does not show through the thumb.
    layer.enabled: !enabled

    handle: Rectangle {
        objectName: root.objectName.length > 0 ? root.objectName + "Handle" : "sliderHandle"
        x: root.leftPadding + root.visualPosition * (root.availableWidth - width)
        y: root.trackCenterY - height / 2
        implicitWidth: 28
        implicitHeight: 20
        radius: height / 2
        color: root.thumbColor
        border.width: 1
        border.color: Theme.color.controlBorder

        FocusBorder {
            visible: root.visualFocus
            targetRadius: parent.radius
        }
    }

    background: Rectangle {
        x: root.leftPadding + root.handle.width / 2
        y: root.trackCenterY - height / 2
        width: Math.max(0, root.availableWidth - root.handle.width)
        height: 6
        radius: Theme.radius.xs
        color: root.trackColor

        Rectangle {
            width: root.visualPosition * parent.width
            height: parent.height
            radius: parent.radius
            color: root.trackHighlightColor
        }

        Repeater {
            model: root.showsTickMarks ? root.stepCount + 1 : 0

            Rectangle {
                required property int index
                x: index / root.stepCount * parent.width - width / 2
                y: parent.height + Theme.space.s
                width: 3
                height: 3
                radius: width / 2
                color: Theme.color.neutral6
            }
        }

        Repeater {
            model: root.tickLabels

            CoreText {
                required property var modelData
                x: (modelData.value - root.from) / (root.to - root.from) * parent.width - width / 2
                y: parent.height + Theme.space.ml
                text: modelData.text
                textStyle: Theme.text.caption
                color: Theme.color.neutral7
                Accessible.ignored: true
            }
        }
    }
}

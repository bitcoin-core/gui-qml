// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Control {
    id: root

    property var model: []
    property int currentIndex: 0
    // Set by a hosting FormRow, whose label names the picker.
    property string rowTitle: ""
    property string rowDescription: ""
    readonly property string currentText: model.length > currentIndex ? optionText(model[currentIndex]) : ""

    signal selected(int index, var option)

    function moveSelection(index) {
        if (index < 0 || index >= root.model.length) return
        const segment = segments.itemAt(index)
        if (!segment || !segment.enabled) return
        segment.forceActiveFocus(Qt.TabFocusReason)
        root.selected(index, root.model[index])
    }

    function optionText(option) {
        if (typeof option === "object" && option !== null && option.text !== undefined) {
            return option.text
        }
        return option
    }

    implicitHeight: Theme.size.control
    implicitWidth: 360
    padding: 5
    Accessible.role: Accessible.PageTabList
    Accessible.name: rowTitle
    Accessible.description: rowDescription

    background: Rectangle {
        color: Theme.color.neutral2
        radius: Theme.radius.m

        Behavior on color {
            enabled: Theme.motion.animated
            ColorAnimation { duration: Theme.motion.normal }
        }
    }

    // A positioner keeps changing options out of the Qt 6.4 layout engine.
    contentItem: Row {
        id: segmentsRow
        spacing: 5

        Repeater {
            id: segments
            model: root.model

            ToggleButton {
                required property int index
                required property var modelData

                objectName: typeof modelData === "object" && modelData !== null && modelData.objectName !== undefined
                    ? modelData.objectName
                    : (root.objectName.length > 0 ? root.objectName + "Option_" + index : "")
                width: Math.max(0, (segmentsRow.width - segmentsRow.spacing * (root.model.length - 1)) / Math.max(1, root.model.length))
                height: segmentsRow.height
                autoExclusive: true
                checked: index === root.currentIndex
                text: root.optionText(modelData)
                bgRadius: Theme.radius.s
                textColor: Theme.color.neutral9
                textHoverColor: checked ? Theme.color.white : Theme.color.neutral9
                textActiveColor: Theme.color.white
                textActiveBold: true
                bgHoverColor: checked ? Theme.color.orange : Theme.color.neutral4
                bgActiveColor: Theme.color.orange
                bgDefaultColor: Theme.color.neutral2
                Accessible.role: Accessible.PageTab
                Keys.onLeftPressed: root.moveSelection(index - 1)
                Keys.onRightPressed: root.moveSelection(index + 1)

                onClicked: {
                    root.selected(index, modelData)
                }
            }
        }
    }
}

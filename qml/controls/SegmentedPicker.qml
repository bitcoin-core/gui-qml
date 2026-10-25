// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

TabBar {
    id: root

    property var model: []
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
    spacing: 5
    Accessible.role: Accessible.PageTabList
    Accessible.name: rowTitle
    Accessible.description: rowDescription

    background: Rectangle {
        color: Theme.color.surface2
        radius: Theme.radius.m

        Behavior on color {
            enabled: Theme.motion.animated
            ColorAnimation { duration: Theme.motion.normal }
        }
    }

    // A view keeps changing options out of the Qt 6.4 layout engine.
    contentItem: ListView {
        model: root.contentModel
        currentIndex: root.currentIndex
        spacing: root.spacing
        orientation: ListView.Horizontal
        interactive: false
    }

    Repeater {
        id: segments
        model: root.model

        Segment {
            required property int index
            required property var modelData

            objectName: typeof modelData === "object" && modelData !== null && modelData.objectName !== undefined
                ? modelData.objectName
                : (root.objectName.length > 0 ? root.objectName + "Option_" + index : "")
            width: Math.max(0, (root.availableWidth - root.spacing * (root.model.length - 1)) / Math.max(1, root.model.length))
            height: root.availableHeight
            activeFocusOnTab: checked
            autoExclusive: true
            checked: index === root.currentIndex
            text: root.optionText(modelData)
            Accessible.role: Accessible.PageTab
            Keys.onLeftPressed: root.moveSelection(index - 1)
            Keys.onRightPressed: root.moveSelection(index + 1)

            onClicked: {
                root.selected(index, modelData)
            }
        }
    }

    // The selected segment is the one orange control in the app.
    component Segment: TabButton {
        id: segment

        checkable: true
        hoverEnabled: AppMode.isDesktop
        focusPolicy: Qt.StrongFocus
        leftPadding: Theme.space.ml
        rightPadding: Theme.space.ml
        topPadding: 5
        bottomPadding: 5

        contentItem: CoreText {
            text: segment.text
            textStyle: segment.checked ? Theme.text.captionStrong : Theme.text.caption
            color: segment.checked ? Theme.color.accentForeground : Theme.color.neutral9

            Behavior on color {
                enabled: Theme.motion.animated
                ColorAnimation { duration: Theme.motion.normal }
            }
        }

        background: Rectangle {
            color: segment.checked ? Theme.color.accent
                : segment.hovered ? Theme.color.surface3
                : Theme.color.surface2
            radius: Theme.radius.s

            Behavior on color {
                enabled: Theme.motion.animated
                ColorAnimation { duration: Theme.motion.normal }
            }
        }

        FocusBorder {
            objectName: segment.objectName.length > 0 ? segment.objectName + "FocusBorder" : ""
            visible: segment.visualFocus
            targetRadius: Theme.radius.s
            z: 1
        }
    }
}

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
    readonly property string currentText: model.length > currentIndex ? optionText(model[currentIndex]) : ""

    signal selected(int index, var option)

    function optionText(option) {
        if (typeof option === "object" && option !== null && option.text !== undefined) {
            return option.text
        }
        return option
    }

    implicitHeight: 45
    implicitWidth: 360
    padding: 5

    background: Rectangle {
        color: Theme.color.neutral2
        radius: 8

        Behavior on color {
            ColorAnimation { duration: 150 }
        }
    }

    contentItem: RowLayout {
        spacing: 5

        Repeater {
            model: root.model

            ToggleButton {
                required property int index
                required property var modelData

                objectName: root.objectName.length > 0 ? root.objectName + "Option_" + index : ""
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 1
                Layout.minimumWidth: 0
                autoExclusive: true
                checked: index === root.currentIndex
                text: root.optionText(modelData)
                bgRadius: 5
                textColor: Theme.color.neutral9
                textHoverColor: checked ? Theme.color.white : Theme.color.neutral9
                textActiveColor: Theme.color.white
                textActiveBold: true
                bgHoverColor: checked ? Theme.color.orange : Theme.color.neutral4
                bgActiveColor: Theme.color.orange
                bgDefaultColor: Theme.color.neutral2

                onClicked: {
                    root.selected(index, modelData)
                }
            }
        }
    }
}

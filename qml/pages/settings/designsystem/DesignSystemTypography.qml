// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15

import "../../../controls"

DesignSystemSection {
    id: root

    // Read the roles from the TextSet itself so a new role cannot be left out.
    function roleNames(textSet) {
        const names = []
        for (const name in textSet) {
            const value = textSet[name]
            if (value !== null && typeof value === "object" && value.pixelSize !== undefined) names.push(name)
        }
        return names
    }

    readonly property var textRoles: roleNames(Theme.text)

    title: "Typography"
    description: "Every Theme.text role in the active type set (" + Theme.text.family + ", " + Theme.text.monoFamily
        + "), read from the TextSet. The shaded box is the line box set by lineHeight."

    // Positioners keep changing Repeater delegates out of the Qt 6.4 layout engine.
    Column {
        id: typographyColumn
        objectName: "designSystemTextRoles"
        Layout.fillWidth: true
        spacing: 16

        Repeater {
            model: root.textRoles

            delegate: ColumnLayout {
                id: typographySample
                required property string modelData
                readonly property var textStyle: Theme.text[modelData]
                objectName: "designSystemText_" + modelData
                width: typographyColumn.width
                spacing: 4

                Item {
                    Layout.fillWidth: true
                    implicitHeight: sampleText.implicitHeight

                    Rectangle {
                        width: sampleText.contentWidth
                        height: parent.height
                        color: Theme.color.neutral2
                    }

                    CoreText {
                        id: sampleText
                        width: parent.width
                        text: typographySample.modelData
                        color: Theme.color.neutral9
                        font: typographySample.textStyle.font
                        lineHeight: typographySample.textStyle.lineHeight
                        lineHeightMode: Text.FixedHeight
                        horizontalAlignment: Text.AlignLeft
                        wrap: false
                        elide: Text.ElideRight
                    }
                }

                CoreText {
                    Layout.fillWidth: true
                    text: typographySample.textStyle.family + " " + typographySample.textStyle.styleName
                        + " · " + typographySample.textStyle.pixelSize + "/" + typographySample.textStyle.lineHeight
                    color: Theme.color.neutral7
                    font: Theme.text.monoCaption.font
                    lineHeight: Theme.text.monoCaption.lineHeight
                    lineHeightMode: Text.FixedHeight
                    horizontalAlignment: Text.AlignLeft
                    wrap: false
                    elide: Text.ElideRight
                }
            }
        }
    }
}

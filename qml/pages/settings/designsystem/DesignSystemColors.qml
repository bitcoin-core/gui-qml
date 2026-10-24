// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15

import "../../../controls"

DesignSystemSection {
    id: root

    function isColorList(value) {
        return value !== null && typeof value === "object" && typeof value.length === "number"
    }

    // Read the roles from the ColorSet itself so a new role cannot be left out.
    function roleNames(colorSet) {
        const names = []
        for (const name in colorSet) {
            if (name === "objectName" || typeof colorSet[name] === "function") continue
            names.push(name)
        }
        return names
    }

    readonly property var allRoles: roleNames(Theme.color)
    readonly property var colorRoles: allRoles.filter(name => !isColorList(Theme.color[name]))
    readonly property var colorListRoles: allRoles.filter(name => isColorList(Theme.color[name]))

    title: "Colors"
    description: "Every Theme.color role of the active palette, read from the ColorSet. Switch the theme to compare."

    DesignSystemSpecimen {
        objectName: "designSystemColorRoles"
        Layout.fillWidth: true
        name: "Color roles"
        sampleSpacing: 12

        Repeater {
            model: root.colorRoles

            delegate: RowLayout {
                id: colorSample
                required property string modelData
                objectName: "designSystemColor_" + modelData
                width: 200
                spacing: 10

                Rectangle {
                    Layout.preferredWidth: 32
                    Layout.preferredHeight: 32
                    radius: 4
                    color: Theme.color[colorSample.modelData]
                    border.color: Theme.color.neutral4
                    border.width: 1
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    CoreText {
                        Layout.fillWidth: true
                        text: colorSample.modelData
                        color: Theme.color.neutral9
                        font: Theme.text.caption.font
                        lineHeight: Theme.text.caption.lineHeight
                        lineHeightMode: Text.FixedHeight
                        horizontalAlignment: Text.AlignLeft
                        wrap: false
                        elide: Text.ElideRight
                    }

                    CoreText {
                        Layout.fillWidth: true
                        text: Theme.color[colorSample.modelData].toString().toUpperCase()
                        color: Theme.color.neutral7
                        font: Theme.text.monoCaption.font
                        lineHeight: Theme.text.monoCaption.lineHeight
                        lineHeightMode: Text.FixedHeight
                        horizontalAlignment: Text.AlignLeft
                        wrap: false
                    }
                }
            }
        }
    }

    DesignSystemSpecimen {
        objectName: "designSystemColorLists"
        Layout.fillWidth: true
        name: "Gradients and ramps"
        note: "Border gradients run top to bottom; confirmationColors run from 0 to 5 confirmations."
        sampleSpacing: 20

        Repeater {
            model: root.colorListRoles

            delegate: ColumnLayout {
                id: listSample
                required property string modelData
                readonly property var colors: Theme.color[modelData]
                objectName: "designSystemColor_" + modelData
                spacing: 6

                CoreText {
                    Layout.fillWidth: true
                    text: listSample.modelData
                    color: Theme.color.neutral9
                    font: Theme.text.caption.font
                    lineHeight: Theme.text.caption.lineHeight
                    lineHeightMode: Text.FixedHeight
                    horizontalAlignment: Text.AlignLeft
                    wrap: false
                }

                RowLayout {
                    spacing: 8

                    Rectangle {
                        Layout.preferredWidth: 32
                        Layout.preferredHeight: 32
                        radius: 4
                        border.color: Theme.color.neutral4
                        border.width: 1
                        gradient: Gradient {
                            GradientStop { position: 0; color: listSample.colors[0] }
                            GradientStop { position: 1; color: listSample.colors[listSample.colors.length - 1] }
                        }
                    }

                    // Positioners keep changing Repeater delegates out of the Qt 6.4 layout engine.
                    Row {
                        spacing: 4

                        Repeater {
                            model: listSample.colors

                            delegate: Column {
                                id: stopSample
                                required property var modelData
                                spacing: 2

                                Rectangle {
                                    width: 64
                                    height: 16
                                    radius: 3
                                    color: stopSample.modelData
                                    border.color: Theme.color.neutral4
                                    border.width: 1
                                }

                                CoreText {
                                    text: String(stopSample.modelData).toUpperCase()
                                    color: Theme.color.neutral7
                                    font: Theme.text.monoCaption.font
                                    lineHeight: Theme.text.monoCaption.lineHeight
                                    lineHeightMode: Text.FixedHeight
                                    horizontalAlignment: Text.AlignLeft
                                    wrap: false
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

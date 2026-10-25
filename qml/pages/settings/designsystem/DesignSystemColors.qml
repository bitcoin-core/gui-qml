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

    // Read the roles from the palette itself so a new role cannot be left out.
    function roleNames(palette) {
        const names = []
        for (const name in palette) {
            const type = typeof palette[name]
            if (name === "objectName" || type === "function" || type === "string" || type === "boolean") continue
            names.push(name)
        }
        return names
    }

    readonly property var baseRoles: [
        "neutral0", "neutral1", "neutral2", "neutral3", "neutral4",
        "neutral5", "neutral6", "neutral7", "neutral8", "neutral9",
        "orange", "red", "green", "blue", "amber", "purple", "lavender"
    ]
    // Names from before the semantic roles, and the role each one reads.
    readonly property var legacyRoles: ({
        white: "accentForeground",
        orangeLight1: "accentHover",
        orangeLight2: "accentPressed",
        neutral1SurfaceBorderGradient: "surface1BorderGradient",
        neutral2SurfaceBorderGradient: "surface2BorderGradient",
        primaryButtonBorderGradient: "accentBorderGradient",
        destructiveButtonBorderGradient: "dangerBorderGradient"
    })

    readonly property var allRoles: roleNames(Theme.color)
    readonly property var semanticRoles: allRoles.filter(name => !isColorList(Theme.color[name])
        && baseRoles.indexOf(name) < 0 && legacyRoles[name] === undefined)
    readonly property var colorListRoles: allRoles.filter(name => isColorList(Theme.color[name])
        && legacyRoles[name] === undefined)

    title: "Colors"
    description: "Every Theme.color role of the active palette (" + Theme.color.paletteId
        + "), read from the palette. Pick another palette above to compare."

    component ColorSwatch: RowLayout {
        id: swatch
        required property string role
        readonly property var value: Theme.color[role]
        readonly property bool isList: value !== null && typeof value === "object" && typeof value.length === "number"
        property string detail: isList ? "" : value.toString().toUpperCase()
        objectName: "designSystemColor_" + role
        width: 200
        spacing: 10

        Rectangle {
            Layout.preferredWidth: 32
            Layout.preferredHeight: 32
            radius: 4
            color: swatch.isList ? "transparent" : swatch.value
            gradient: swatch.isList ? listGradient : null
            border.color: Theme.color.neutral4
            border.width: 1

            Gradient {
                id: listGradient
                GradientStop { position: 0; color: swatch.isList ? swatch.value[0] : "transparent" }
                GradientStop { position: 1; color: swatch.isList ? swatch.value[swatch.value.length - 1] : "transparent" }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0

            CoreText {
                Layout.fillWidth: true
                text: swatch.role
                color: Theme.color.neutral9
                textStyle: Theme.text.caption
                horizontalAlignment: Text.AlignLeft
                wrap: false
                elide: Text.ElideRight
            }

            CoreText {
                Layout.fillWidth: true
                text: swatch.detail
                color: Theme.color.neutral7
                textStyle: Theme.text.monoCaption
                horizontalAlignment: Text.AlignLeft
                wrap: false
                elide: Text.ElideRight
            }
        }
    }

    // One nesting level: its fill, the state roles composited over it, a divider, then the next level.
    component SurfaceBox: Rectangle {
        id: box
        required property int level
        default property alias nested: nestedColumn.data
        readonly property string roleName: level === 0 ? "background" : "surface" + level
        objectName: "designSystemSurface_" + roleName
        implicitHeight: boxColumn.implicitHeight + 24
        radius: level === 0 ? 16 : 12
        color: Theme.color.surface(level)
        border.color: level === 0 ? Theme.color.neutral4 : "transparent"
        border.width: level === 0 ? 1 : 0

        ColumnLayout {
            id: boxColumn
            x: 12
            y: 12
            width: box.width - 24
            spacing: 10

            CoreText {
                Layout.fillWidth: true
                text: box.roleName + "  ·  surface(" + box.level + ")  ·  "
                    + Theme.color.surface(box.level).toString().toUpperCase()
                color: Theme.color.neutral9
                textStyle: Theme.text.captionStrong
                horizontalAlignment: Text.AlignLeft
                wrap: false
                elide: Text.ElideRight
            }

            // A positioner keeps the Repeater out of the Qt 6.4 layout engine.
            Row {
                spacing: 6

                Repeater {
                    model: ["surfaceHover", "surfacePressed", "surfaceSelected"]

                    delegate: Rectangle {
                        id: stateSample
                        required property string modelData
                        objectName: "designSystemSurfaceState_" + box.roleName + "_" + modelData
                        width: 120
                        height: 28
                        radius: 5
                        color: Theme.color[modelData]

                        CoreText {
                            anchors.centerIn: parent
                            text: stateSample.modelData
                            color: Theme.color.neutral9
                            textStyle: Theme.text.caption
                            wrap: false
                        }
                    }
                }
            }

            Rectangle {
                objectName: "designSystemSurfaceDivider_" + box.roleName
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: Theme.color.surfaceDivider
            }

            ColumnLayout {
                id: nestedColumn
                visible: children.length > 0
                Layout.fillWidth: true
                spacing: 0
            }
        }
    }

    DesignSystemSpecimen {
        objectName: "designSystemColorBase"
        Layout.fillWidth: true
        name: "Base scale and hues"
        note: "Every palette defines these; the roles below default to them."
        sampleSpacing: 12

        Repeater {
            model: root.baseRoles
            delegate: ColorSwatch {
                required property string modelData
                role: modelData
            }
        }
    }

    DesignSystemSpecimen {
        objectName: "designSystemColorRoles"
        Layout.fillWidth: true
        name: "Semantic roles"
        note: "Translucent roles are shown over the page background."
        sampleSpacing: 12

        Repeater {
            model: root.semanticRoles
            delegate: ColorSwatch {
                required property string modelData
                role: modelData
            }
        }
    }

    DesignSystemSpecimen {
        objectName: "designSystemSurfaces"
        Layout.fillWidth: true
        name: "Surfaces"
        note: "Each level sits inside the one before. surfaceHover, surfacePressed, surfaceSelected and surfaceDivider are the foreground at a low alpha, composited over every level."

        SurfaceBox {
            width: Math.min(560, parent ? parent.width : 560)
            level: 0

            SurfaceBox {
                Layout.fillWidth: true
                level: 1

                SurfaceBox {
                    Layout.fillWidth: true
                    level: 2

                    SurfaceBox {
                        Layout.fillWidth: true
                        level: 3
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
                    textStyle: Theme.text.caption
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
                                    textStyle: Theme.text.monoCaption
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

    DesignSystemSpecimen {
        objectName: "designSystemColorAliases"
        Layout.fillWidth: true
        name: "Old names"
        note: "Kept until their call sites move to the semantic role they read."
        sampleSpacing: 12

        Repeater {
            model: Object.keys(root.legacyRoles)
            delegate: ColorSwatch {
                required property string modelData
                width: 300
                role: modelData
                detail: "= " + root.legacyRoles[modelData]
            }
        }
    }
}

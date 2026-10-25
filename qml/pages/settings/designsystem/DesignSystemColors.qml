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
            if (name === "objectName" || type === "function" || type === "string" || type === "boolean" || type === "number") continue
            names.push(name)
        }
        return names
    }

    function hex(color) {
        const channel = function(value) { return ("0" + Math.round(value * 255).toString(16)).slice(-2) }
        return ("#" + channel(color.r) + channel(color.g) + channel(color.b)).toUpperCase()
    }

    function valueText(value) {
        if (isColorList(value)) return value.length + " stops"
        return value.a < 0.995 ? hex(value) + " " + Math.round(value.a * 100) + "%" : hex(value)
    }

    // The roles that refine a hue, such as accentHover or dangerTint, found by name.
    function variantsOf(hue) {
        return allRoles.filter(name => name !== hue && name.indexOf(hue) === 0 && !isColorList(Theme.color[name]))
    }

    function variantLabel(hue, name) {
        const rest = name.slice(hue.length)
        return rest.charAt(0).toLowerCase() + rest.slice(1)
    }

    readonly property var allRoles: roleNames(Theme.color)
    readonly property var baseRoles: [
        "neutral0", "neutral1", "neutral2", "neutral3", "neutral4",
        "neutral5", "neutral6", "neutral7", "neutral8", "neutral9"
    ]
    readonly property var surfaceRoles: [
        { role: "background", use: "Window and pages" },
        { role: "surface1", use: "Cards and grouped rows" },
        { role: "surface2", use: "Fields and cards in cards" },
        { role: "surface3", use: "Deepest nesting" }
    ]
    readonly property var stateRoles: [
        { role: "surfaceHover", use: "Pointer over a row or button" },
        { role: "surfacePressed", use: "While pressed" },
        { role: "surfaceSelected", use: "Persistent selection" },
        { role: "surfaceDivider", use: "Rules between rows" }
    ]
    readonly property var textRoles: [
        { role: "neutral9", sample: "Primary text and titles", use: "Titles, values, primary labels" },
        { role: "neutral8", sample: "Menu items and row labels", use: "Menus, sidebar, labels" },
        { role: "neutral7", sample: "Supporting descriptions", use: "Descriptions, metadata" },
        { role: "neutral6", sample: "Quiet captions", use: "Tertiary text" },
        { role: "placeholder", sample: "Placeholder text", use: "Hints inside fields" },
        { role: "disabledContent", sample: "Disabled content", use: "Unavailable controls" }
    ]
    readonly property var hueRoles: [
        { role: "accent", use: "Primary actions, selected segment" },
        { role: "danger", use: "Errors and destructive actions" },
        { role: "success", use: "Confirmations, money in" },
        { role: "info", use: "Notices and help" },
        { role: "warning", use: "Caution, needs attention" },
        { role: "paymentRequest", use: "Payment requests in activity" }
    ]
    readonly property var activityGroups: [
        { title: "Wallet activity", rows: [
            { role: "txIncoming", label: "Received" },
            { role: "txOutgoing", label: "Sent" },
            { role: "txInternal", label: "To yourself" } ] },
        { title: "Network traffic", rows: [
            { role: "trafficSent", label: "Sent" },
            { role: "trafficReceived", label: "Received" } ] }
    ]
    readonly property var utilityUses: ({
        focus: "Keyboard focus ring",
        selection: "Selected text in fields",
        selectedText: "Text inside a selection",
        controlBorder: "Checkbox, field and switch borders",
        activeFill: "Applied filters and toggles",
        activeForeground: "Content on activeFill",
        menuBorder: "Menu outline",
        menuDivider: "Lines between menu groups",
        modalOverlay: "Behind every modal",
        qrBackground: "QR code background",
        qrForeground: "QR code modules",
        syntaxKey: "Keys in RPC console output",
        illustrationSignal: "Onboarding signal pulse",
        illustrationLatestBlock: "Onboarding newest block",
        surface1BorderGradient: "Edge of raised surface1",
        surface2BorderGradient: "Edge of raised surface2",
        accentBorderGradient: "Edge of accent buttons",
        dangerBorderGradient: "Edge of danger buttons",
        confirmationColors: "Block clock, 0 to 5 confirmations"
    })
    // Everything the groups above don't show, so a new role still appears.
    readonly property var utilityRoles: {
        const shown = baseRoles.slice()
        for (const list of [surfaceRoles, stateRoles, textRoles, hueRoles]) {
            for (const entry of list) shown.push(entry.role)
        }
        for (const hue of hueRoles) shown.push(...variantsOf(hue.role))
        for (const group of activityGroups) {
            for (const entry of group.rows) shown.push(entry.role)
        }
        return allRoles.filter(name => shown.indexOf(name) < 0)
    }

    description: "Every role of the active palette (" + Theme.color.paletteId + "), grouped by what it is for. "
        + "Variants such as hover, pressed and text are shown with the color they come from."
    contentSpacing: Theme.space.page

    // A name, its value and what it is for. On narrow widths the value and use move under the name.
    component RoleRow: GridLayout {
        id: row
        property string role
        property string label: role
        property string usage: ""
        property bool showsSwatch: false
        property var labelStyle: Theme.text.description
        property color labelColor: Theme.color.neutral9
        readonly property var value: Theme.color[role]
        readonly property bool compact: SizeClass.isCompact(width)
        readonly property int swatchColumns: showsSwatch ? 1 : 0

        columns: swatchColumns + (compact ? 2 : 3)
        columnSpacing: Theme.space.ml
        rowSpacing: Theme.space.xxs

        Rectangle {
            visible: row.showsSwatch
            Layout.preferredWidth: Theme.size.icon.m
            Layout.preferredHeight: Theme.size.icon.m
            Layout.alignment: Qt.AlignVCenter
            radius: Theme.radius.xs
            color: root.isColorList(row.value) ? "transparent" : row.value
            border.color: Theme.color.surfaceDivider
            border.width: 1
            gradient: root.isColorList(row.value) ? rowGradient : null

            Gradient {
                id: rowGradient
                GradientStop { position: 0; color: root.isColorList(row.value) ? row.value[0] : "transparent" }
                GradientStop { position: 1; color: root.isColorList(row.value) ? row.value[row.value.length - 1] : "transparent" }
            }
        }

        CoreText {
            Layout.fillWidth: true
            text: row.label
            color: row.labelColor
            textStyle: row.labelStyle
            horizontalAlignment: Text.AlignLeft
        }

        CoreText {
            Layout.preferredWidth: row.compact ? -1 : 170
            text: root.valueText(row.value)
            color: Theme.color.neutral6
            textStyle: Theme.text.monoCaption
            horizontalAlignment: row.compact ? Text.AlignRight : Text.AlignLeft
            wrap: false
        }

        CoreText {
            visible: row.usage.length > 0
            Layout.columnSpan: row.compact ? row.columns : 1
            Layout.fillWidth: row.compact
            Layout.preferredWidth: row.compact ? -1 : 220
            Layout.leftMargin: row.compact && row.showsSwatch ? Theme.size.icon.m + row.columnSpacing : 0
            text: row.usage
            color: Theme.color.neutral7
            textStyle: Theme.text.caption
            horizontalAlignment: Text.AlignLeft
        }
    }

    // A labelled swatch block for the surfaces.
    component SwatchCard: Column {
        id: card
        property string role
        property string usage
        readonly property color value: Theme.color[role]
        objectName: "designSystemColor_" + role
        spacing: Theme.space.ml

        Rectangle {
            width: card.width
            height: 88
            radius: Theme.radius.cardInset
            color: card.value
            border.color: Theme.color.surfaceDivider
            border.width: 1
        }

        Column {
            width: card.width
            spacing: Theme.space.xxs

            CoreText {
                width: parent.width
                text: card.role
                color: Theme.color.neutral9
                textStyle: Theme.text.description
                horizontalAlignment: Text.AlignLeft
            }

            CoreText {
                width: parent.width
                text: root.valueText(card.value)
                color: Theme.color.neutral6
                textStyle: Theme.text.monoCaption
                horizontalAlignment: Text.AlignLeft
            }

            CoreText {
                width: parent.width
                topPadding: Theme.space.xs
                text: card.usage
                color: Theme.color.neutral7
                textStyle: Theme.text.caption
                horizontalAlignment: Text.AlignLeft
            }
        }
    }

    // A hue on top of a surface1 card, with its variants as chips.
    readonly property Component hueCardDelegate: Item {
        id: hueCard
        required property var modelData
        width: hueFlow.itemWidth
        property string role: modelData.role
        property string usage: modelData.use
        readonly property color value: Theme.color[role]
        objectName: "designSystemColor_" + role
        height: 72 + hueBody.height

        Rectangle {
            width: parent.width
            height: 72 + radius
            radius: Theme.radius.card
            color: hueCard.value
        }

        Rectangle {
            id: hueBody
            y: 72
            width: parent.width
            height: hueColumn.implicitHeight + Theme.space.ml + Theme.space.l
            radius: Theme.radius.card
            color: Theme.color.surface1

            // Squares off the top corners where the body meets the color block.
            Rectangle {
                width: parent.width
                height: parent.radius
                color: parent.color
            }

            ColumnLayout {
                id: hueColumn
                x: Theme.space.l
                y: Theme.space.ml
                width: parent.width - 2 * Theme.space.l
                spacing: Theme.space.xxs

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.space.m

                    CoreText {
                        Layout.fillWidth: true
                        text: hueCard.role
                        color: Theme.color.neutral9
                        textStyle: Theme.text.description
                        horizontalAlignment: Text.AlignLeft
                        wrap: false
                        elide: Text.ElideRight
                    }

                    CoreText {
                        text: root.valueText(hueCard.value)
                        color: Theme.color.neutral6
                        textStyle: Theme.text.monoCaption
                        wrap: false
                    }
                }

                CoreText {
                    Layout.fillWidth: true
                    text: hueCard.usage
                    color: Theme.color.neutral7
                    textStyle: Theme.text.caption
                    horizontalAlignment: Text.AlignLeft
                }

                // A positioner keeps the Repeater out of the Qt 6.4 layout engine.
                Flow {
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.space.ml
                    spacing: Theme.space.ml

                    Repeater {
                        model: root.variantsOf(hueCard.role)

                        delegate: Row {
                            id: chip
                            required property string modelData
                            readonly property color value: Theme.color[modelData]
                            objectName: "designSystemColor_" + modelData
                            spacing: Theme.space.s

                            Rectangle {
                                anchors.verticalCenter: parent.verticalCenter
                                width: Theme.size.icon.xs
                                height: Theme.size.icon.xs
                                radius: Theme.radius.xs
                                color: chip.value
                                border.color: Theme.color.surfaceDivider
                                border.width: 1
                            }

                            CoreText {
                                text: root.variantLabel(hueCard.role, chip.modelData)
                                color: Theme.color.neutral6
                                textStyle: Theme.text.monoCaption
                                wrap: false
                            }
                        }
                    }
                }
            }
        }
    }

    DesignSystemGroup {
        objectName: "designSystemColorBase"
        Layout.fillWidth: true
        title: "Base scale"
        description: "Set by each palette. Surfaces and text default to these steps."

        Column {
            id: ramp
            Layout.fillWidth: true
            spacing: Theme.space.s
            readonly property real stepWidth: width / root.baseRoles.length

            Item {
                width: ramp.width
                height: 56

                Row {
                    anchors.fill: parent

                    Repeater {
                        model: root.baseRoles

                        delegate: Item {
                            id: step
                            required property string modelData
                            required property int index
                            readonly property color value: Theme.color[modelData]
                            readonly property bool first: index === 0
                            readonly property bool last: index === root.baseRoles.length - 1
                            objectName: "designSystemColor_" + modelData
                            width: ramp.stepWidth
                            height: parent.height

                            Rectangle {
                                anchors.fill: parent
                                radius: step.first || step.last ? Theme.radius.cardInset : 0
                                color: step.value
                            }

                            // Squares off the inner corners of the two rounded ends.
                            Rectangle {
                                visible: step.first || step.last
                                x: step.first ? parent.width / 2 : 0
                                width: parent.width / 2
                                height: parent.height
                                color: step.value
                            }
                        }
                    }
                }

                Rectangle {
                    anchors.fill: parent
                    radius: Theme.radius.cardInset
                    color: "transparent"
                    border.color: Theme.color.surfaceDivider
                    border.width: 1
                }
            }

            Row {
                Repeater {
                    model: root.baseRoles.length

                    delegate: CoreText {
                        required property int index
                        width: ramp.stepWidth
                        text: index
                        color: Theme.color.neutral6
                        textStyle: Theme.text.monoCaption
                        horizontalAlignment: Text.AlignLeft
                        wrap: false
                    }
                }
            }
        }
    }

    DesignSystemGroup {
        objectName: "designSystemColorSurfaces"
        Layout.fillWidth: true
        title: "Surfaces"
        description: "One level per nesting depth. Controls pick theirs from surfaceLevel."

        Flow {
            id: surfaceFlow
            Layout.fillWidth: true
            spacing: Theme.space.l
            readonly property int columns: SizeClass.isCompact(width) ? 2 : 4
            readonly property real itemWidth: Math.floor((width - spacing * (columns - 1)) / columns)

            Repeater {
                model: root.surfaceRoles

                delegate: SwatchCard {
                    required property var modelData
                    width: surfaceFlow.itemWidth
                    role: modelData.role
                    usage: modelData.use
                }
            }
        }
    }

    DesignSystemGroup {
        objectName: "designSystemColorStates"
        Layout.fillWidth: true
        title: "States and dividers"
        description: "Translucent foreground, so they read on every surface. Shown here on surface1."

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: stateColumn.implicitHeight + 2 * Theme.space.m
            radius: Theme.radius.card
            color: Theme.color.surface1

            Column {
                id: stateColumn
                x: Theme.space.m
                y: Theme.space.m
                width: parent.width - 2 * Theme.space.m

                Repeater {
                    model: root.stateRoles

                    delegate: Rectangle {
                        id: stateRow
                        required property var modelData
                        readonly property bool divider: modelData.role === "surfaceDivider"
                        objectName: "designSystemColor_" + modelData.role
                        readonly property color value: Theme.color[modelData.role]
                        width: stateColumn.width
                        height: stateContent.implicitHeight + 2 * Theme.space.ml
                        radius: Theme.radius.cardInset
                        color: divider ? "transparent" : value

                        ColumnLayout {
                            id: stateContent
                            x: Theme.space.l
                            y: Theme.space.ml
                            width: parent.width - 2 * Theme.space.l
                            spacing: Theme.space.ml

                            RoleRow {
                                Layout.fillWidth: true
                                role: stateRow.modelData.role
                                usage: stateRow.modelData.use
                            }

                            Rectangle {
                                visible: stateRow.divider
                                Layout.fillWidth: true
                                Layout.preferredHeight: 1
                                color: stateRow.value
                            }
                        }
                    }
                }
            }
        }
    }

    DesignSystemGroup {
        objectName: "designSystemColorText"
        Layout.fillWidth: true
        title: "Text and icons"
        description: "From most to least emphasis."

        Column {
            id: textColumn
            Layout.fillWidth: true

            Repeater {
                model: root.textRoles

                delegate: Column {
                    id: textRow
                    required property var modelData
                    required property int index
                    objectName: modelData.role.indexOf("neutral") === 0 ? "designSystemTextColor_" + modelData.role
                                                                         : "designSystemColor_" + modelData.role
                    readonly property color value: Theme.color[modelData.role]
                    width: textColumn.width
                    topPadding: Theme.space.ml
                    spacing: Theme.space.ml

                    RoleRow {
                        width: textColumn.width
                        role: textRow.modelData.role
                        label: textRow.modelData.sample
                        usage: textRow.modelData.use
                        labelStyle: Theme.text.body
                        labelColor: textRow.value
                    }

                    Rectangle {
                        visible: textRow.index < root.textRoles.length - 1
                        width: textColumn.width
                        height: 1
                        color: Theme.color.surfaceDivider
                    }
                }
            }
        }
    }

    DesignSystemGroup {
        objectName: "designSystemColorStatus"
        Layout.fillWidth: true
        title: "Accent and status"
        description: "Color that carries meaning. Always paired with text, an icon or a shape."

        Flow {
            id: hueFlow
            Layout.fillWidth: true
            spacing: Theme.space.l
            readonly property int columns: Math.max(1, Math.floor((width + spacing) / (300 + spacing)))
            readonly property real itemWidth: Math.floor((width - spacing * (columns - 1)) / columns)

            Repeater {
                model: root.hueRoles

                delegate: root.hueCardDelegate
            }
        }
    }

    DesignSystemGroup {
        objectName: "designSystemColorActivity"
        Layout.fillWidth: true
        title: "Activity and data"
        description: "Wallet activity and network traffic, always with an arrow or a label."

        Flow {
            id: activityFlow
            Layout.fillWidth: true
            spacing: Theme.space.l
            readonly property int columns: Math.max(1, Math.floor((width + spacing) / (320 + spacing)))
            readonly property real itemWidth: Math.floor((width - spacing * (columns - 1)) / columns)

            Repeater {
                model: root.activityGroups

                delegate: Rectangle {
                    id: activityCard
                    required property var modelData
                    width: activityFlow.itemWidth
                    height: activityColumn.implicitHeight + 2 * Theme.space.l
                    radius: Theme.radius.card
                    color: Theme.color.surface1

                    Column {
                        id: activityColumn
                        x: Theme.space.l
                        y: Theme.space.l
                        width: parent.width - 2 * Theme.space.l
                        spacing: Theme.space.ml

                        CoreText {
                            width: parent.width
                            text: activityCard.modelData.title
                            color: Theme.color.neutral6
                            textStyle: Theme.text.captionStrong
                            horizontalAlignment: Text.AlignLeft
                        }

                        Repeater {
                            model: activityCard.modelData.rows

                            delegate: RowLayout {
                                id: activityRow
                                required property var modelData
                                readonly property color value: Theme.color[modelData.role]
                                objectName: "designSystemColor_" + modelData.role
                                width: activityColumn.width
                                spacing: Theme.space.m

                                Rectangle {
                                    Layout.preferredWidth: 10
                                    Layout.preferredHeight: 10
                                    radius: 5
                                    color: activityRow.value
                                }

                                CoreText {
                                    Layout.fillWidth: true
                                    text: activityRow.modelData.label
                                    color: Theme.color.neutral9
                                    textStyle: Theme.text.description
                                    horizontalAlignment: Text.AlignLeft
                                }

                                CoreText {
                                    text: activityRow.modelData.role
                                    color: Theme.color.neutral6
                                    textStyle: Theme.text.monoCaption
                                    wrap: false
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    DesignSystemGroup {
        objectName: "designSystemColorUtility"
        Layout.fillWidth: true
        title: "Controls and utility"
        description: "Roles used by one kind of control, and the gradients and ramps. Rarely set by hand."

        Column {
            id: utilityColumn
            Layout.fillWidth: true

            Repeater {
                model: root.utilityRoles

                delegate: Column {
                    id: utilityRow
                    required property string modelData
                    required property int index
                    readonly property var value: Theme.color[modelData]
                    objectName: "designSystemColor_" + modelData
                    width: utilityColumn.width
                    topPadding: Theme.space.m
                    spacing: Theme.space.m

                    RoleRow {
                        width: utilityColumn.width
                        role: utilityRow.modelData
                        usage: root.utilityUses[utilityRow.modelData] || ""
                        showsSwatch: true
                    }

                    Rectangle {
                        visible: utilityRow.index < root.utilityRoles.length - 1
                        width: utilityColumn.width
                        height: 1
                        color: Theme.color.surfaceDivider
                    }
                }
            }
        }
    }
}

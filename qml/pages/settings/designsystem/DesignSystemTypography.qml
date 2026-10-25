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

    function weightName(weight) {
        return weight === Font.DemiBold ? "Semi Bold" : weight === Font.Normal ? "Regular" : String(weight)
    }

    readonly property var textRoles: roleNames(Theme.text)
    readonly property var samples: ({
        display: "Sync complete",
        headline: "Send bitcoin",
        title: "Transaction details",
        subtitle: "Speed up transaction",
        heading: "Network traffic",
        subheading: "Connection",
        lead: "Your node is up to date with the network.",
        bodyLarge: "Choose how much disk space to use.",
        body: "The fee rate decides how soon it confirms.",
        description: "Allow incoming peer connections.",
        caption: "Last block received 2 minutes ago",
        captionStrong: "Group by month",
        button: "Cancel",
        buttonStrong: "Send",
        menuItem: "Copy address",
        menuItemStrong: "All wallets",
        monoLead: "4a5e1e4b…eda33b",
        monoBody: "bc1qar0srrr7xfkvy5l643lydnw9re59gtzzwf5mdq",
        monoDescription: "Satoshi:27.0.0/",
        monoCaption: "~/Library/Application Support/Bitcoin",
        amountLead: "0.01250000 BTC",
        amountBody: "1 250 000 sat",
        amountDescription: "0.00042000 BTC",
        amountCaption: "0.00000546 BTC"
    })
    readonly property var groups: [
        { name: "Headings", id: "Headings",
          description: "Page, section and dialog titles.",
          roles: ["display", "headline", "title", "subtitle", "heading", "subheading"] },
        { name: "Reading", id: "Reading",
          description: "Explanations, row labels, descriptions and captions.",
          roles: ["lead", "bodyLarge", "body", "description", "caption", "captionStrong"] },
        { name: "Interface", id: "Interface",
          description: "Set by buttons and menus themselves.",
          roles: ["button", "buttonStrong", "menuItem", "menuItemStrong"] },
        { name: "Mono", id: "Mono",
          description: "Literal values: identifiers, addresses, paths and console output. " + Theme.text.monoFamily + ".",
          roles: ["monoLead", "monoBody", "monoDescription", "monoCaption"] },
        { name: "Amounts", id: "Amounts",
          description: "Bitcoin amounts, in the font the Display setting picks. " + Theme.text.amountBody.family + " now.",
          roles: ["amountLead", "amountBody", "amountDescription", "amountCaption"] }
    ]
    // Roles the groups above don't list, so a new role still appears.
    readonly property var otherRoles: {
        const listed = []
        for (const group of groups) listed.push(...group.roles)
        return textRoles.filter(name => listed.indexOf(name) < 0)
    }

    description: "Text roles at their real size. Pick a role by purpose; its size, line height and weight come with it "
        + "and follow the Text size setting."
    contentSpacing: Theme.space.page

    // A sample in the role, with its name and metrics beside it, or under it on narrow widths.
    component TypeRow: Column {
        id: typeRow
        property string role
        property bool showsDivider: true
        readonly property var textStyle: Theme.text[role]
        readonly property bool compact: SizeClass.isCompact(width)
        objectName: "designSystemText_" + role
        topPadding: Theme.space.l
        spacing: Theme.space.l

        GridLayout {
            width: typeRow.width
            columns: typeRow.compact ? 1 : 2
            columnSpacing: Theme.space.xxl
            rowSpacing: Theme.space.s

            CoreText {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                text: root.samples[typeRow.role] || typeRow.role
                color: Theme.color.neutral9
                textStyle: typeRow.textStyle
                horizontalAlignment: Text.AlignLeft
                // Addresses and paths have no spaces to break at.
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
            }

            GridLayout {
                Layout.fillWidth: typeRow.compact
                Layout.preferredWidth: typeRow.compact ? -1 : 190
                Layout.alignment: Qt.AlignVCenter
                columns: typeRow.compact ? 2 : 1
                columnSpacing: Theme.space.ml
                rowSpacing: Theme.space.xxs

                CoreText {
                    text: typeRow.role
                    color: Theme.color.neutral9
                    textStyle: Theme.text.description
                    horizontalAlignment: Text.AlignLeft
                    wrap: false
                }

                CoreText {
                    Layout.fillWidth: true
                    text: typeRow.textStyle.pixelSize + " / " + typeRow.textStyle.lineHeight
                        + " · " + root.weightName(typeRow.textStyle.weight)
                    color: Theme.color.neutral6
                    textStyle: Theme.text.monoCaption
                    horizontalAlignment: Text.AlignLeft
                }
            }
        }

        Rectangle {
            visible: typeRow.showsDivider
            width: typeRow.width
            height: 1
            color: Theme.color.surfaceDivider
        }
    }

    CoreText {
        objectName: "designSystemTypeSet"
        Layout.fillWidth: true
        text: "TypeSet \"" + Theme.typeSet.typeSetId + "\" · " + Theme.text.family + " · " + Theme.text.monoFamily
        color: Theme.color.neutral6
        textStyle: Theme.text.monoCaption
        horizontalAlignment: Text.AlignLeft
    }

    // Positioners keep the Repeaters out of the Qt 6.4 layout engine.
    Column {
        Layout.fillWidth: true
        spacing: Theme.space.page

        Repeater {
            model: root.groups

            delegate: DesignSystemGroup {
                id: typeGroup
                required property var modelData
                objectName: "designSystemText" + modelData.id
                shows: ["CoreText"]
                width: parent ? parent.width : 0
                title: modelData.name
                description: modelData.description

                Column {
                    id: typeColumn
                    Layout.fillWidth: true

                    Repeater {
                        model: typeGroup.modelData.roles

                        delegate: TypeRow {
                            required property string modelData
                            required property int index
                            width: typeColumn.width
                            role: modelData
                            showsDivider: index < typeGroup.modelData.roles.length - 1
                        }
                    }
                }
            }
        }

        DesignSystemGroup {
            id: otherGroup
            objectName: "designSystemTextOther"
            visible: root.otherRoles.length > 0
            width: parent.width
            title: "Other"
            description: "Roles not in a group above yet."

            Column {
                id: otherColumn
                Layout.fillWidth: true

                Repeater {
                    model: root.otherRoles

                    delegate: TypeRow {
                        required property string modelData
                        required property int index
                        width: otherColumn.width
                        role: modelData
                        showsDivider: index < root.otherRoles.length - 1
                    }
                }
            }
        }
    }
}

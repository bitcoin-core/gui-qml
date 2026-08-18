pragma ComponentBehavior: Bound

// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15
import "../../controls"

SettingsPage {
    id: root
    title: qsTr("Design system")

    readonly property var typographyRoles: [
        {
            name: "display",
            group: "Headers"
        },
        {
            name: "headline",
            group: "Headers"
        },
        {
            name: "title",
            group: "Headers"
        },
        {
            name: "subtitle",
            group: "Headers"
        },
        {
            name: "heading",
            group: "Headers"
        },
        {
            name: "subheading",
            group: "Headers"
        },
        {
            name: "lead",
            group: "Body"
        },
        {
            name: "bodyLarge",
            group: "Body"
        },
        {
            name: "body",
            group: "Body"
        },
        {
            name: "description",
            group: "Body"
        },
        {
            name: "caption",
            group: "Body"
        },
        {
            name: "button",
            group: "Controls"
        },
        {
            name: "buttonStrong",
            group: "Controls"
        },
        {
            name: "monoLead",
            group: "Mono"
        },
        {
            name: "monoBody",
            group: "Mono"
        },
        {
            name: "monoDescription",
            group: "Mono"
        },
        {
            name: "monoCaption",
            group: "Mono"
        }
    ]

    readonly property var paletteTokens: ["background", "white", "orange", "orangeLight1", "orangeLight2", "red", "green", "blue", "amber", "purple", "neutral0", "neutral1", "neutral2", "neutral3", "neutral4", "neutral5", "neutral6", "neutral7", "neutral8", "neutral9"]

    property string exampleLanguage: "en"
    property string exampleBlockClockMode: "compact"
    property bool exampleStartupEnabled: true
    property string exampleBlockStorageLimit: "2"
    property string exampleProxyAddress: "127.0.0.1:9050"
    property url lastExampleLink: ""

    // ── Form controls ───────────────────────────────────────
    PageHeading {
        Layout.fillWidth: true
        title: qsTr("General")
        description: qsTr("Generic form and navigation components using the active Theme tokens.")
    }

    FormSection {
        objectName: "designSystemAppearanceSection"
        Layout.fillWidth: true
        title: qsTr("Appearance")

        FormRow {
            objectName: "designSystemThemeRow"
            Layout.fillWidth: true
            title: qsTr("Theme")
            description: qsTr("Choose the application appearance.")
            trailingItem: SegmentedPicker {
                implicitWidth: 190
                implicitHeight: 36
                model: [qsTr("Light"), qsTr("Dark")]
                currentIndex: Theme.dark ? 1 : 0
                onSelected: function (index, option) {
                    Theme.dark = index === 1;
                }
            }
        }

        FormRow {
            objectName: "designSystemLanguageRow"
            Layout.fillWidth: true
            title: qsTr("Language")
            description: qsTr("Choose the language used throughout the app.")
            showDivider: false
            trailingItem: PopupPicker {
                objectName: "designSystemLanguagePicker"
                embedded: true
                minimumMenuWidth: 180
                currentValue: root.exampleLanguage
                model: [
                    {
                        text: qsTr("English"),
                        value: "en"
                    },
                    {
                        text: qsTr("Deutsch"),
                        value: "de"
                    },
                    {
                        text: qsTr("Español"),
                        value: "es"
                    }
                ]
                onActivated: function (value) {
                    root.exampleLanguage = value;
                }
            }
        }
    }

    FormSection {
        objectName: "designSystemBehaviorSection"
        Layout.fillWidth: true
        title: qsTr("Behavior")
        description: qsTr("Rows can host any existing control without owning its state.")

        FormRow {
            objectName: "designSystemBlockClockRow"
            Layout.fillWidth: true
            title: qsTr("Block status size")
            description: qsTr("Set the scale used for the block status display.")
            trailingItem: SegmentedPicker {
                implicitWidth: 220
                implicitHeight: 36
                model: [
                    {
                        text: qsTr("Compact"),
                        value: "compact"
                    },
                    {
                        text: qsTr("Showcase"),
                        value: "showcase"
                    }
                ]
                currentIndex: root.exampleBlockClockMode === "compact" ? 0 : 1
                onSelected: function (index, option) {
                    root.exampleBlockClockMode = option.value;
                }
            }
        }

        FormRow {
            objectName: "designSystemStartupRow"
            Layout.fillWidth: true
            title: qsTr("Open at login")
            description: qsTr("Start the app after signing in.")
            showDivider: false
            trailingItem: OptionSwitch {
                objectName: "designSystemStartupSwitch"
                checked: root.exampleStartupEnabled
                onToggled: root.exampleStartupEnabled = checked
            }
        }
    }

    FormSection {
        objectName: "designSystemNavigationSection"
        Layout.fillWidth: true
        title: qsTr("Navigation rows")
        description: qsTr("Use ListRow for destinations and disclosure actions.")

        ListRow {
            objectName: "designSystemSelectedListRow"
            Layout.fillWidth: true
            title: qsTr("Selected destination")
            description: qsTr("Selection and keyboard focus are independent states.")
            selected: true
            showsDisclosureIndicator: true
            disclosureIndicatorColor: Theme.color.orange
        }

        ListRow {
            objectName: "designSystemDisclosureListRow"
            Layout.fillWidth: true
            title: qsTr("Advanced options")
            description: qsTr("Open another page for settings that need more space.")
            showDivider: false
            showsDisclosureIndicator: true
        }
    }

    FormSection {
        objectName: "designSystemValueSection"
        Layout.fillWidth: true
        title: qsTr("Values and links")
        description: qsTr("Use value rows for read-only data and link rows for caller-owned navigation.")

        LinkRow {
            objectName: "designSystemWebsiteRow"
            Layout.fillWidth: true
            title: qsTr("Website")
            value: "bitcoincore.org"
            link: "https://bitcoincore.org"
            onActivated: function (link) {
                root.lastExampleLink = link;
            }
        }

        LinkRow {
            objectName: "designSystemSourceRow"
            Layout.fillWidth: true
            title: qsTr("Source code")
            value: "github.com/bitcoin/bitcoin"
            link: "https://github.com/bitcoin/bitcoin"
            onActivated: function (link) {
                root.lastExampleLink = link;
            }
        }

        ValueRow {
            objectName: "designSystemVersionRow"
            Layout.fillWidth: true
            title: qsTr("Version")
            value: "v31.99.0-unk"
            showDivider: false
        }
    }

    FormSection {
        objectName: "designSystemFieldSection"
        Layout.fillWidth: true
        title: qsTr("Inline fields and details")
        description: qsTr("Use compact trailing editors for short values and body content for long details.")

        TextFieldRow {
            objectName: "designSystemBlockStorageRow"
            fieldObjectName: "designSystemBlockStorageField"
            Layout.fillWidth: true
            title: qsTr("Block storage limit (GB)")
            fieldWidth: 72
            text: root.exampleBlockStorageLimit
            validator: IntValidator {
                bottom: 1
            }
            onTextEdited: function (text) {
                root.exampleBlockStorageLimit = text;
            }
        }

        TextFieldRow {
            objectName: "designSystemProxyLocationRow"
            fieldObjectName: "designSystemProxyLocationField"
            Layout.fillWidth: true
            title: qsTr("Proxy location")
            fieldWidth: 200
            text: root.exampleProxyAddress
            onTextEdited: function (text) {
                root.exampleProxyAddress = text;
            }
        }

        FormRow {
            objectName: "designSystemDataDirectoryRow"
            Layout.fillWidth: true
            title: qsTr("Data directory")
            supportingText: qsTr("Selected before startup. The data directory cannot be changed while the node is running.")
            showDivider: false
            bodyItem: CoreText {
                objectName: "designSystemDataDirectoryValue"
                Layout.fillWidth: true
                text: "/Users/example/Bitcoin"
                color: Theme.color.neutral7
                font: Theme.text.caption.font
                lineHeight: Theme.text.caption.lineHeight
                lineHeightMode: Text.FixedHeight
                horizontalAlignment: Text.AlignLeft
                wrap: true
            }
        }
    }

    // ── Typography ──────────────────────────────────────────
    Text {
        Layout.topMargin: 8
        Layout.fillWidth: true
        font: Theme.text.title.font
        color: Theme.color.neutral9
        text: qsTr("Typography")
    }

    Repeater {
        model: root.typographyRoles
        delegate: ColumnLayout {
            id: typographySample
            required property var modelData
            Layout.fillWidth: true
            spacing: 4

            Text {
                Layout.fillWidth: true
                font: Theme.text[typographySample.modelData.name].font
                lineHeight: Theme.text[typographySample.modelData.name].lineHeight
                lineHeightMode: Text.FixedHeight
                color: Theme.color.neutral9
                text: typographySample.modelData.name
                elide: Text.ElideRight
            }
            Text {
                Layout.fillWidth: true
                font: Theme.text.caption.font
                color: Theme.color.neutral6
                text: Theme.text[typographySample.modelData.name].family + " " + Theme.text[typographySample.modelData.name].styleName + " · " + Theme.text[typographySample.modelData.name].pixelSize + "/" + Theme.text[typographySample.modelData.name].lineHeight + " · " + typographySample.modelData.group
            }
            Rectangle {
                Layout.topMargin: 8
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: Theme.color.neutral3
            }
        }
    }

    // ── Colors ──────────────────────────────────────────────
    Text {
        Layout.topMargin: 16
        Layout.fillWidth: true
        font: Theme.text.title.font
        color: Theme.color.neutral9
        text: qsTr("Colors")
    }
    Text {
        Layout.fillWidth: true
        font: Theme.text.caption.font
        color: Theme.color.neutral6
        text: qsTr("Palette tokens for the active theme. Toggle Theme to compare.")
        wrapMode: Text.WordWrap
    }

    GridLayout {
        Layout.fillWidth: true
        columns: 2
        columnSpacing: 12
        rowSpacing: 8

        Repeater {
            model: root.paletteTokens
            delegate: RowLayout {
                id: paletteSample
                required property string modelData
                Layout.fillWidth: true
                spacing: 10

                Rectangle {
                    Layout.preferredWidth: 32
                    Layout.preferredHeight: 32
                    radius: 4
                    color: Theme.color[paletteSample.modelData]
                    border.color: Theme.color.neutral4
                    border.width: 1
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    Text {
                        Layout.fillWidth: true
                        font: Theme.text.description.font
                        color: Theme.color.neutral9
                        text: paletteSample.modelData
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        font: Theme.text.caption.font
                        color: Theme.color.neutral6
                        text: Theme.color[paletteSample.modelData].toString().toUpperCase()
                    }
                }
            }
        }
    }

    Item {
        Layout.preferredHeight: 24
    }
}

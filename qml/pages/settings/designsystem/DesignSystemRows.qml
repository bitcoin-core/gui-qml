// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15

import "../../../controls"

DesignSystemSection {
    id: root

    property string exampleLanguage: "en"
    property string exampleBlockClockMode: "compact"
    property bool exampleStartupEnabled: true
    property bool exampleNotificationsEnabled: false
    property bool exampleAcknowledged: false
    property url lastExampleLink: ""

    title: "Rows and sections"
    description: "FormSection cards and the rows settings pages are built from. Rows host controls without owning their state."

    DesignSystemSpecimen {
        objectName: "designSystemFormSection"
        Layout.fillWidth: true
        name: "FormSection"

        DesignSystemState {
            width: 380
            label: "title, description, footerText"
            FormSection {
                Layout.fillWidth: true
                title: "Section title"
                description: "Description under the title."
                footerText: "Footer text under the card."

                FormRow { Layout.fillWidth: true; title: "First row" }
                FormRow { Layout.fillWidth: true; title: "Last row"; showDivider: false }
            }
        }
        DesignSystemState {
            width: 380
            label: "isOnSurface: true, nested"
            FormSection {
                Layout.fillWidth: true
                title: "Outer section"

                FormSection {
                    Layout.fillWidth: true
                    Layout.margins: 12
                    isOnSurface: true
                    title: "isOnSurface"

                    FormRow { Layout.fillWidth: true; title: "First row" }
                    FormRow { Layout.fillWidth: true; title: "Last row"; showDivider: false }
                }
            }
        }
        DesignSystemState {
            width: 380
            label: "showBackground: false"
            FormSection {
                Layout.fillWidth: true
                showBackground: false
                title: "No card"

                FormRow { Layout.fillWidth: true; title: "First row" }
                FormRow { Layout.fillWidth: true; title: "Last row"; showDivider: false }
            }
        }
    }

    FormSection {
        objectName: "designSystemFormRowSection"
        Layout.fillWidth: true
        title: "FormRow"
        description: "Title, description, supporting and error text, leading, trailing and body items."

        FormRow {
            Layout.fillWidth: true
            title: "Title only"
        }

        FormRow {
            Layout.fillWidth: true
            title: "Title and description"
            description: "Description in caption."
        }

        FormRow {
            Layout.fillWidth: true
            title: "Supporting text"
            supportingText: "Supporting text in blue."
        }

        FormRow {
            Layout.fillWidth: true
            title: "Error text"
            errorText: "Error text replaces the supporting text."
        }

        FormRow {
            Layout.fillWidth: true
            title: "Disabled"
            description: "enabled: false"
            enabled: false
            trailingItem: OptionSwitch { checked: true }
        }

        FormRow {
            Layout.fillWidth: true
            title: "Leading item"
            leadingItem: Icon {
                source: "image://images/wallet"
                color: Theme.color.neutral9
                size: 24
            }
        }

        FormRow {
            objectName: "designSystemLanguageRow"
            Layout.fillWidth: true
            title: "Language"
            description: "Trailing PopupPicker."
            trailingItem: PopupPicker {
                objectName: "designSystemLanguagePicker"
                embedded: true
                minimumMenuWidth: 180
                currentValue: root.exampleLanguage
                model: [
                    { text: "English", value: "en" },
                    { text: "Deutsch", value: "de" },
                    { text: "Español", value: "es" }
                ]
                onActivated: function(value) {
                    root.exampleLanguage = value
                }
            }
        }

        FormRow {
            objectName: "designSystemDataDirectoryRow"
            Layout.fillWidth: true
            title: "Data directory"
            supportingText: "Selected before startup. The data directory cannot be changed while the node is running."
            showDivider: false
            bodyItem: CoreText {
                objectName: "designSystemDataDirectoryValue"
                Layout.fillWidth: true
                text: "/Users/example/Bitcoin"
                color: Theme.color.neutral7
                font: Theme.text.monoCaption.font
                lineHeight: Theme.text.monoCaption.lineHeight
                lineHeightMode: Text.FixedHeight
                horizontalAlignment: Text.AlignLeft
                wrap: true
            }
        }
    }

    FormSection {
        objectName: "designSystemSwitchRowSection"
        Layout.fillWidth: true
        title: "Switch rows"
        description: "Settings that apply immediately use a FormRow with a trailing OptionSwitch or SegmentedPicker."

        FormRow {
            objectName: "designSystemStartupRow"
            Layout.fillWidth: true
            title: "Open at login"
            description: "Start the app after signing in."
            trailingItem: OptionSwitch {
                objectName: "designSystemStartupSwitch"
                checked: root.exampleStartupEnabled
                onToggled: root.exampleStartupEnabled = checked
            }
        }

        FormRow {
            Layout.fillWidth: true
            title: "Notifications"
            trailingItem: OptionSwitch {
                checked: root.exampleNotificationsEnabled
                onToggled: root.exampleNotificationsEnabled = checked
            }
        }

        FormRow {
            objectName: "designSystemBlockClockRow"
            Layout.fillWidth: true
            title: "Block status size"
            description: "Set the scale used for the block status display."
            showDivider: false
            trailingItem: SegmentedPicker {
                implicitWidth: 220
                implicitHeight: 36
                model: [
                    { text: "Compact", value: "compact" },
                    { text: "Showcase", value: "showcase" }
                ]
                currentIndex: root.exampleBlockClockMode === "compact" ? 0 : 1
                onSelected: function(index, option) {
                    root.exampleBlockClockMode = option.value
                }
            }
        }

        ListRow {
            objectName: "designSystemAcknowledgementRow"
            Layout.fillWidth: true
            description: "Acknowledgements are a ListRow that toggles its trailing switch; the row is the only focus stop."
            showDivider: false
            accessibleRole: Accessible.CheckBox
            trailingItem: OptionSwitch {
                checked: root.exampleAcknowledged
                focusPolicy: Qt.NoFocus
                Accessible.ignored: true
                onToggled: root.exampleAcknowledged = checked
            }
            onClicked: root.exampleAcknowledged = !root.exampleAcknowledged
        }
    }

    FormSection {
        objectName: "designSystemListRowSection"
        Layout.fillWidth: true
        title: "ListRow"
        description: "Destinations and disclosure actions. Selection and keyboard focus are independent states."

        ListRow {
            objectName: "designSystemSelectedListRow"
            Layout.fillWidth: true
            title: "Selected destination"
            description: "selected: true"
            selected: true
            showsDisclosureIndicator: true
            disclosureIndicatorColor: Theme.color.orange
        }

        ListRow {
            objectName: "designSystemDisclosureListRow"
            Layout.fillWidth: true
            title: "Advanced options"
            description: "showsDisclosureIndicator: true"
            showsDisclosureIndicator: true
        }

        ListRow {
            Layout.fillWidth: true
            title: "Leading item"
            leadingItem: Icon {
                source: "image://images/gear"
                color: Theme.color.neutral9
                size: 24
            }
            showsDisclosureIndicator: true
        }

        ListRow {
            Layout.fillWidth: true
            title: "Disabled"
            description: "enabled: false"
            enabled: false
            showDivider: false
            showsDisclosureIndicator: true
        }
    }

    FormSection {
        objectName: "designSystemValueSection"
        Layout.fillWidth: true
        title: "ValueRow and LinkRow"
        description: "Value rows show read-only data; link rows hand their link to the caller."

        LinkRow {
            objectName: "designSystemWebsiteRow"
            Layout.fillWidth: true
            title: "Website"
            value: "bitcoincore.org"
            link: "https://bitcoincore.org"
            onActivated: function(link) {
                root.lastExampleLink = link
            }
        }

        LinkRow {
            Layout.fillWidth: true
            title: "Disabled link"
            value: "github.com/bitcoin/bitcoin"
            link: "https://github.com/bitcoin/bitcoin"
            enabled: false
        }

        ValueRow {
            objectName: "designSystemVersionRow"
            Layout.fillWidth: true
            title: "Version"
            value: "v31.99.0-unk"
        }

        ValueRow {
            Layout.fillWidth: true
            title: "Value icon"
            value: "Connected"
            valueIconSource: "image://images/check"
        }

        ValueRow {
            objectName: "designSystemAmountValueRow"
            Layout.fillWidth: true
            title: "Amount"
            amountValue: true
            value: "0.00150000"
            valueUnit: "BTC"
            valueTextStyle: Theme.text.monoDescription
        }

        ValueRow {
            Layout.fillWidth: true
            title: "Disabled"
            value: "v31.99.0-unk"
            enabled: false
            showDivider: false
        }
    }

    DesignSystemSpecimen {
        objectName: "designSystemKeyValueRow"
        Layout.fillWidth: true
        name: "KeyValueRow"

        DesignSystemState {
            width: 380
            label: "key, value"
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6

                KeyValueRow {
                    Layout.fillWidth: true
                    key: CoreText {
                        text: "Fee rate"
                        color: Theme.color.neutral7
                        font: Theme.text.description.font
                        lineHeight: Theme.text.description.lineHeight
                        lineHeightMode: Text.FixedHeight
                        horizontalAlignment: Text.AlignLeft
                    }
                    value: CoreText {
                        text: "12 sat/vB"
                        font: Theme.text.monoDescription.font
                        lineHeight: Theme.text.monoDescription.lineHeight
                        lineHeightMode: Text.FixedHeight
                        horizontalAlignment: Text.AlignLeft
                    }
                }

                KeyValueRow {
                    Layout.fillWidth: true
                    key: CoreText {
                        text: "Size"
                        color: Theme.color.neutral7
                        font: Theme.text.description.font
                        lineHeight: Theme.text.description.lineHeight
                        lineHeightMode: Text.FixedHeight
                        horizontalAlignment: Text.AlignLeft
                    }
                    value: CoreText {
                        text: "141 vB"
                        font: Theme.text.monoDescription.font
                        lineHeight: Theme.text.monoDescription.lineHeight
                        lineHeightMode: Text.FixedHeight
                        horizontalAlignment: Text.AlignLeft
                    }
                }
            }
        }
    }
}

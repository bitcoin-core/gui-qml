pragma ComponentBehavior: Bound

// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import org.bitcoincore.qt 1.0

import "../../controls"

SettingsPage {
    id: root
    objectName: "displaySettingsPage"
    title: qsTranslate("SettingsDisplay", "Display")
    showBackButton: false

    FormSection {
        Layout.fillWidth: true
        title: qsTr("Appearance")

        FormRow {
            objectName: "displayThemeRow"
            Layout.fillWidth: true
            title: qsTranslate("SettingsDisplay", "Theme")
            trailingItem: SegmentedPicker {
                objectName: "displayThemePicker"
                implicitWidth: 190
                implicitHeight: 36
                model: [qsTr("Light"), qsTr("Dark")]
                currentIndex: Theme.dark ? 1 : 0
                onSelected: function(index, option) {
                    Theme.dark = index === 1
                }
            }
        }

        FormRow {
            objectName: "displayBlockStatusSizeRow"
            Layout.fillWidth: true
            title: qsTranslate("SettingsDisplay", "Block status size")
            trailingItem: PopupPicker {
                objectName: "displayBlockStatusSizePicker"
                embedded: true
                minimumMenuWidth: 520
                subtitleRole: "description"
                iconRole: "icon"
                iconSize: 40
                currentValue: Theme.blockclocksize >= 1 / 2 ? 1 / 2 : 1 / 3
                model: [
                    {
                        text: qsTr("Compact"),
                        value: 1 / 3,
                        description: qsTr("For personal use on a computer or smartphone."),
                        icon: "image://images/blockclock-size-compact"
                    },
                    {
                        text: qsTr("Showcase"),
                        value: 1 / 2,
                        description: qsTr("A larger block clock for public display on a tablet or other large screen."),
                        icon: "image://images/blockclock-size-showcase"
                    }
                ]
                onActivated: function(value) {
                    Theme.blockclocksize = value
                }
            }
        }

        FormRow {
            objectName: "displayMoneyFontRow"
            Layout.fillWidth: true
            title: qsTr("Money font")
            showDivider: false
            trailingItem: PopupPicker {
                objectName: "displayMoneyFontPicker"
                embedded: true
                minimumMenuWidth: 400
                subtitleRole: "description"
                currentValue: optionsModel.moneyFontChoice
                model: [
                    {
                        text: qsTr("Roboto Mono"),
                        value: "embedded",
                        description: qsTr("Included with Bitcoin Core")
                    },
                    {
                        text: qsTr("System Monospace"),
                        value: "best_system",
                        description: qsTr("Uses your operating system’s default monospaced font")
                    }
                ]
                onActivated: function(value) {
                    optionsModel.moneyFontChoice = value
                }
            }
        }
    }

    FormSection {
        Layout.fillWidth: true
        title: qsTr("Language and format")

        FormRow {
            objectName: "displayUnitRow"
            Layout.fillWidth: true
            title: qsTranslate("SettingsDisplay", "Display unit")
            trailingItem: PopupPicker {
                objectName: "displayUnitPicker"
                embedded: true
                minimumMenuWidth: 400
                subtitleRole: "description"
                objectNameRole: "objectName"
                currentValue: optionsModel.displayUnit
                model: [
                    {
                        text: qsTr("BTC"),
                        value: 0,
                        objectName: "displayUnitBTC",
                        description: qsTr("8 decimal places (0.00000001 BTC = 1 sat)")
                    },
                    {
                        text: qsTr("mBTC"),
                        value: 1,
                        objectName: "displayUnitMBTC",
                        description: qsTr("5 decimal places (0.00001 mBTC = 1 sat)")
                    },
                    {
                        text: qsTr("bits"),
                        value: 2,
                        objectName: "displayUnitBits",
                        description: qsTr("2 decimal places (0.01 bits = 1 sat)")
                    },
                    {
                        text: qsTr("sat"),
                        value: 3,
                        objectName: "displayUnitSAT",
                        description: qsTr("Satoshi, the smallest unit (1 sat = 0.00000001 BTC)")
                    }
                ]
                onActivated: function(value) {
                    optionsModel.displayUnit = value
                }
            }
        }

        ListRow {
            objectName: "displayLanguageRow"
            Layout.fillWidth: true
            title: qsTranslate("SettingsDisplay", "Language")
            enabled: ((optionsModel.coreSettingStatuses || ({})).lang || ({})).canEdit !== false
            showsDisclosureIndicator: true
            disclosureIndicatorObjectName: "displayLanguageDisclosureIndicator"
            trailingItem: CoreText {
                text: optionsModel.languageLabel(optionsModel.language)
                color: Theme.color.neutral7
                font: Theme.text.description.font
            }
            onClicked: root.StackView.view.push(languagePage)
        }

        ListRow {
            objectName: "displayTransactionUrlsRow"
            Layout.fillWidth: true
            title: qsTr("Third-party transaction URLs")
            showDivider: false
            showsDisclosureIndicator: true
            onClicked: root.StackView.view.push(transactionUrlsPage)
        }
    }

    FormSection {
        objectName: "displayDeveloperSection"
        Layout.fillWidth: true
        visible: BuildInfo.isDebug
        title: qsTr("Developer")

        ListRow {
            objectName: "displayDesignSystemRow"
            Layout.fillWidth: true
            title: qsTr("Design system")
            description: qsTr("Preview reusable controls and design tokens.")
            showDivider: false
            showsDisclosureIndicator: true
            onClicked: root.StackView.view.push(designSystemPage)
        }
    }

    Component {
        id: languagePage

        SettingsLanguage {
            onBack: root.StackView.view.pop()
        }
    }

    Component {
        id: designSystemPage

        SettingsDesignSystem {
            objectName: "displayDesignSystemPage"
            onBack: root.StackView.view.pop()
        }
    }

    Component {
        id: transactionUrlsPage

        SettingsPage {
            id: transactionUrls
            title: qsTr("Transaction URLs")
            maximumContentWidth: 560
            onBack: transactionUrls.StackView.view.pop()

            PageHeading {
                Layout.fillWidth: true
                title: qsTr("Third-party transaction URLs")
                description: qsTr("Use %s for the transaction hash. Separate multiple URLs with |.")
            }

            CoreTextField {
                objectName: "thirdPartyTransactionUrlsInput"
                Layout.fillWidth: true
                text: optionsModel.thirdPartyTransactionUrls
                placeholderText: "https://example.com/tx/%s"
                onEditingFinished: optionsModel.thirdPartyTransactionUrls = text
            }
        }
    }

}

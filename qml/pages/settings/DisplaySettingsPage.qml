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
    readonly property bool systemReducesMotion: MotionPreference.mode === "reduced"
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
                model: {
                    const names = []
                    for (let i = 0; i < Theme.palettes.length; ++i) {
                        names.push(Theme.palettes[i].displayName)
                    }
                    return names
                }
                // The active palette, not paletteId: an unknown stored id shows its fallback.
                currentIndex: {
                    for (let i = 0; i < Theme.palettes.length; ++i) {
                        if (Theme.palettes[i] === Theme.color) return i
                    }
                    return 0
                }
                onSelected: function(index, option) {
                    Theme.paletteId = Theme.palettes[index].paletteId
                }
            }
        }

        FormRow {
            objectName: "displayBlockStatusSizeRow"
            Layout.fillWidth: true
            title: qsTranslate("SettingsDisplay", "Block status size")
            trailingItem: PopupPicker {
                objectName: "displayBlockStatusSizePicker"
                surfaceLevel: 1
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
                surfaceLevel: 1
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
                        text: qsTr("System monospace"),
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
        objectName: "displayAccessibilitySection"
        Layout.fillWidth: true
        title: qsTr("Accessibility")

        FormRow {
            objectName: "displayTextSizeRow"
            Layout.fillWidth: true
            title: qsTr("Text size")
            bodyItem: RowLayout {
                Layout.fillWidth: true
                spacing: Theme.space.l

                CoreText {
                    Layout.alignment: Qt.AlignTop
                    Layout.topMargin: textSizeSlider.trackCenterY - height / 2
                    text: "A"
                    textStyle: Theme.text.captionStrong
                    color: Theme.color.neutral7
                    Accessible.ignored: true
                }

                Slider {
                    id: textSizeSlider
                    objectName: "displayTextSizeSlider"
                    Layout.fillWidth: true
                    discrete: true
                    showsTickMarks: true
                    from: 0
                    to: Theme.textScales.length - 1
                    // Not a plain binding: dragging writes value, which would break it.
                    Binding on value { value: Math.max(0, Theme.textScales.indexOf(Theme.textScale)) }
                    //: The standard 100% text size, without accessibility scaling.
                    tickLabels: [{ value: Theme.textScales.indexOf(1), text: qsTr("Default") }]
                    Accessible.name: qsTr("Text size")
                    Accessible.description: qsTr("%1%").arg(Math.round(Theme.textScale * 100))
                    onMoved: Theme.textScale = Theme.textScales[Math.round(value)]
                }

                CoreText {
                    Layout.alignment: Qt.AlignTop
                    Layout.topMargin: textSizeSlider.trackCenterY - height / 2
                    objectName: "displayTextSizePercentage"
                    text: qsTr("%1%").arg(Math.round(Theme.textScale * 100))
                    textStyle: Theme.text.captionStrong
                    color: Theme.color.neutral7
                    Accessible.ignored: true
                }
            }
        }

        SwitchRow {
            objectName: "displayReduceMotionRow"
            Layout.fillWidth: true
            title: qsTr("Reduce motion")
            showDivider: false
            description: root.systemReducesMotion ? qsTr("Following your system setting.") : ""
            checked: root.systemReducesMotion || Theme.reduceMotion
            enabled: !root.systemReducesMotion
            onToggled: Theme.reduceMotion = checked
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
                surfaceLevel: 1
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
                textStyle: Theme.text.description
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

            LabeledTextField {
                Layout.fillWidth: true
                label: qsTr("URLs")
                fieldObjectName: "thirdPartyTransactionUrlsInput"
                fieldTextStyle: Theme.text.monoDescription
                text: optionsModel.thirdPartyTransactionUrls
                placeholderText: "https://example.com/tx/%s"
                onEditingFinished: optionsModel.thirdPartyTransactionUrls = text
            }
        }
    }

}

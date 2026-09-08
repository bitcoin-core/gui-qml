// Copyright (c) 2023 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0
import "../../controls"
import "../../components"

Item {
    signal back
    property bool showBackButton: true

    id: root

    PageStack {
        id: displaySettingsView
        anchors.fill: parent

        initialItem: Page {
            id: displaySettings
            background: null
            leftPadding: 20
            rightPadding: 20
            topPadding: 30

            header: SettingsHeader {
                title: qsTr("Display")
                showBackButton: root.showBackButton
                backButtonObjectName: "settingsDisplayBack"
                onBack: root.back()
            }
            ColumnLayout {
                spacing: 4
                width: Math.min(parent.width, 450)
                anchors.horizontalCenter: parent.horizontalCenter
                Setting {
                    id: gotoTheme
                    objectName: "gotoTheme"
                    Layout.fillWidth: true
                    header: qsTr("Theme")
                    actionItem: CaretRightIcon {
                        color: gotoTheme.stateColor
                    }
                    onClicked: {
                        displaySettingsView.push(theme_page)
                    }
                }
                Separator { Layout.fillWidth: true }
                Setting {
                    id: gotoBlockClockSize
                    Layout.fillWidth: true
                    header: qsTr("Block status size")
                    actionItem: CaretRightIcon {
                        color: gotoBlockClockSize.stateColor
                    }
                    onClicked: {
                        displaySettingsView.push(blockclocksize_page)
                    }
                }
                Separator { Layout.fillWidth: true }
                Setting {
                    id: gotoDisplayUnit
                    objectName: "gotoDisplayUnit"
                    Layout.fillWidth: true
                    header: qsTr("Display unit")
                    actionItem: CaretRightIcon {
                        color: gotoDisplayUnit.stateColor
                    }
                    onClicked: {
                        displaySettingsView.push(displayunit_page)
                    }
                }
                Separator { Layout.fillWidth: true }
                Setting {
                    id: gotoLanguage
                    objectName: "gotoLanguage"
                    readonly property var settingStatus: (optionsModel.coreSettingStatuses || ({})).lang || ({})
                    Layout.fillWidth: true
                    header: qsTr("Language")
                    state: settingStatus.canEdit === false ? "DISABLED" : "FILLED"
                    infoText: settingStatus.infoText || ""
                    showInfoText: infoText.length > 0
                    actionItem: CaretRightIcon {
                        color: gotoLanguage.stateColor
                    }
                    onClicked: {
                        displaySettingsView.push(language_page)
                    }
                }
                Separator { Layout.fillWidth: true }
                Setting {
                    id: gotoThirdPartyUrls
                    objectName: "gotoThirdPartyTransactionUrls"
                    Layout.fillWidth: true
                    header: qsTr("Third-party transaction URLs")
                    actionItem: CaretRightIcon {
                        color: gotoThirdPartyUrls.stateColor
                    }
                    onClicked: displaySettingsView.push(third_party_urls_page)
                }
                Separator { Layout.fillWidth: true }
                Setting {
                    id: gotoMoneyFont
                    objectName: "gotoMoneyFont"
                    Layout.fillWidth: true
                    header: qsTr("Money font")
                    actionItem: CaretRightIcon {
                        color: gotoMoneyFont.stateColor
                    }
                    onClicked: displaySettingsView.push(money_font_page)
                }
                CoreText {
                    objectName: "displayLayoutDeveloperSectionLabel"
                    Layout.topMargin: 36
                    Layout.fillWidth: true
                    Layout.leftMargin: 4
                    horizontalAlignment: Text.AlignLeft
                    bold: true
                    font.pixelSize: 13
                    color: Theme.color.neutral7
                    text: qsTr("Developer")
                    visible: AppMode.adaptiveSidebarLayoutAvailable
                }
                Separator {
                    objectName: "displayLayoutDeveloperSectionSeparator"
                    Layout.fillWidth: true
                    visible: AppMode.adaptiveSidebarLayoutAvailable
                }
                Setting {
                    id: gotoApplicationLayout
                    objectName: "gotoApplicationLayout"
                    Layout.fillWidth: true
                    visible: AppMode.adaptiveSidebarLayoutAvailable
                    header: qsTr("Layout")
                    description: AppMode.adaptiveSidebarLayout
                        ? qsTr("[Experimental] Adaptive sidebar layout")
                        : qsTr("Default layout")
                    actionItem: CaretRightIcon {
                        color: gotoApplicationLayout.stateColor
                    }
                    onClicked: displaySettingsView.push(layout_page)
                }
            }
        }
    }
    Component {
        id: layout_page
        SettingsLayout {
            onBack: displaySettingsView.pop()
        }
    }
    Component {
        id: theme_page
        SettingsTheme {
            onBack: {
                displaySettingsView.pop()
            }
            onDesignSystemRequested: {
                displaySettingsView.push(design_system_page)
            }
        }
    }
    Component {
        id: blockclocksize_page
        SettingsBlockClockDisplayMode {
            onBack: {
                displaySettingsView.pop()
            }
        }
    }
    Component {
        id: design_system_page
        SettingsDesignSystem {
            onBack: {
                displaySettingsView.pop()
            }
        }
    }
    Component {
        id: displayunit_page
        SettingsDisplayUnit {
            onBack: {
                displaySettingsView.pop()
            }
        }
    }
    Component {
        id: language_page
        SettingsLanguage {
            onBack: {
                displaySettingsView.pop()
            }
        }
    }
    Component {
        id: third_party_urls_page
        Page {
            background: null
            implicitWidth: 450
            leftPadding: 20
            rightPadding: 20
            topPadding: 30

            header: NavigationBar2 {
                leftItem: NavButton {
                    iconSource: "image://images/caret-left"
                    text: qsTr("Back")
                    onClicked: displaySettingsView.pop()
                }
                centerItem: Header {
                    headerBold: true
                    headerSize: 18
                    header: qsTr("Transaction URLs")
                }
            }

            ColumnLayout {
                spacing: 15
                width: Math.min(parent.width, 450)
                anchors.horizontalCenter: parent.horizontalCenter

                Header {
                    Layout.fillWidth: true
                    center: false
                    header: qsTr("Third-party transaction URLs")
                    headerSize: 18
                    description: qsTr("Use %s for the transaction hash. Separate multiple URLs with |.")
                    descriptionSize: 15
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
    Component {
        id: money_font_page
        Page {
            background: null
            implicitWidth: 450
            leftPadding: 20
            rightPadding: 20
            topPadding: 30

            header: NavigationBar2 {
                leftItem: NavButton {
                    iconSource: "image://images/caret-left"
                    text: qsTr("Back")
                    onClicked: displaySettingsView.pop()
                }
                centerItem: Header {
                    headerBold: true
                    headerSize: 18
                    header: qsTr("Money font")
                }
            }

            ColumnLayout {
                spacing: 15
                width: Math.min(parent.width, 450)
                anchors.horizontalCenter: parent.horizontalCenter

                OptionButton {
                    objectName: "moneyFontEmbedded"
                    Layout.fillWidth: true
                    text: qsTr("Embedded fixed-width font")
                    description: "111.11111111 BTC"
                    checked: optionsModel.moneyFontChoice === "embedded"
                    onClicked: optionsModel.moneyFontChoice = "embedded"
                }

                OptionButton {
                    objectName: "moneyFontSystem"
                    Layout.fillWidth: true
                    text: qsTr("System fixed-width font")
                    description: "111.11111111 BTC"
                    checked: optionsModel.moneyFontChoice === "best_system"
                    onClicked: optionsModel.moneyFontChoice = "best_system"
                }

                CoreText {
                    objectName: "moneyFontPreview"
                    Layout.fillWidth: true
                    text: "111.11111111 BTC"
                    color: Theme.color.neutral9
                    horizontalAlignment: Text.AlignHCenter
                    font.family: optionsModel.moneyFont.family
                    font.weight: optionsModel.moneyFont.weight
                    font.pixelSize: 20
                }
            }
        }
    }
}

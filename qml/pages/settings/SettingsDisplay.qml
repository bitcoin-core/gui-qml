// Copyright (c) 2023 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../../controls"
import "../../components"

Item {
    signal back

    id: root

    PageStack {
        id: displaySettingsView
        anchors.fill: parent

        initialItem: Page {
            id: displaySettings
            background: null
            implicitWidth: 450
            leftPadding: 20
            rightPadding: 20
            topPadding: 30

            header: NavigationBar2 {
                leftItem: NavButton {
                    objectName: "settingsDisplayBack"
                    iconSource: "image://images/caret-left"
                    text: qsTr("Back")
                    onClicked: root.back()
                }
                centerItem: Header {
                    headerBold: true
                    headerSize: 18
                    header: qsTr("Display")
                }
            }
            ColumnLayout {
                spacing: 4
                width: Math.min(parent.width, 450)
                anchors.horizontalCenter: parent.horizontalCenter
                Setting {
                    id: gotoTheme
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
                    description: optionsModel.displayUnitLabel
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
                    Layout.fillWidth: true
                    header: qsTr("Language")
                    // Use qsTr() directly for the system-default label so engine.retranslate()
                    // refreshes it when switching back from a non-default language. C++ tr()
                    // calls in property getters are not re-evaluated by retranslate().
                    description: optionsModel.language === "" ? qsTr("System default") : optionsModel.languageSummary
                    actionItem: CaretRightIcon {
                        color: gotoLanguage.stateColor
                    }
                    onClicked: {
                        displaySettingsView.push(language_page)
                    }
                }
            }
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
}

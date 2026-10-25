// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import "../../../controls"

DesignSystemSection {
    id: root

    description: "Top bars, page headings and tabs. Back buttons here do nothing."

    DesignSystemSpecimen {
        objectName: "designSystemNavigationBar2"
        Layout.fillWidth: true
        name: "NavigationBar2"
        note: "The one top bar: back button (NavButton, 24 px caret), centered heading title that elides, right item and close button. SettingsHeader is a configuration of it."

        DesignSystemState {
            width: parent.width
            label: "showBackButton, title, rightItem"
            NavigationBar2 {
                Layout.fillWidth: true
                showBackButton: true
                title: "Title"
                rightItem: NavButton {
                    text: "Done"
                }
            }
        }
        DesignSystemState {
            width: 380
            label: "icon-only back, showCloseButton"
            NavigationBar2 {
                Layout.fillWidth: true
                showBackButton: true
                backButtonText: ""
                title: "Review"
                showCloseButton: true
            }
        }
        DesignSystemState {
            width: parent.width
            label: "leftItem, rightItem"
            NavigationBar2 {
                Layout.fillWidth: true
                leftItem: NavButton {
                    iconSource: "image://images/gear"
                    Accessible.name: "Settings"
                }
                rightItem: NavButton {
                    text: "Skip"
                }
            }
        }
    }

    DesignSystemSpecimen {
        objectName: "designSystemOnboardingBackButton"
        Layout.fillWidth: true
        name: "OnboardingBackButton"
        note: "The bordered back pill of the onboarding flow."

        DesignSystemState {
            label: "rest"
            OnboardingBackButton {}
        }
        DesignSystemState {
            label: "enabled: false"
            OnboardingBackButton { enabled: false }
        }
    }

    DesignSystemSpecimen {
        objectName: "designSystemSettingsHeader"
        Layout.fillWidth: true
        name: "SettingsHeader"
        note: "The header of every SettingsPage, including this one."

        DesignSystemState {
            width: 380
            label: "compact back button"
            SettingsHeader {
                Layout.fillWidth: true
                title: "Display"
            }
        }
        DesignSystemState {
            width: 380
            label: "backButtonText, rightItem"
            SettingsHeader {
                Layout.fillWidth: true
                title: "Debug log"
                backButtonText: "Settings"
                rightItem: NavButton {
                    iconSource: "image://images/refresh"
                    iconWidth: 20
                    iconHeight: 20
                    Accessible.name: "Refresh"
                }
            }
        }
        DesignSystemState {
            width: 380
            label: "showBackButton: false"
            SettingsHeader {
                Layout.fillWidth: true
                title: "Settings"
                showBackButton: false
            }
        }
    }

    DesignSystemSpecimen {
        objectName: "designSystemPageHeading"
        Layout.fillWidth: true
        name: "PageHeading"

        DesignSystemState {
            width: 380
            label: "title"
            PageHeading { Layout.fillWidth: true; title: "Peers" }
        }
        DesignSystemState {
            width: 380
            label: "title, description"
            PageHeading {
                Layout.fillWidth: true
                title: "Storage"
                description: "Where and how much blockchain data is kept."
            }
        }
        DesignSystemState {
            width: 380
            label: "titleAlignment: AlignHCenter"
            PageHeading {
                Layout.fillWidth: true
                title: "Saving…"
                titleAlignment: Text.AlignHCenter
                description: "The description follows the title (descriptionAlignment)."
            }
        }
        DesignSystemState {
            width: 380
            label: "trailingItem"
            PageHeading {
                Layout.fillWidth: true
                title: "Wallets"
                trailingItem: NeutralButton { text: "Add" }
            }
        }
    }

    DesignSystemSpecimen {
        objectName: "designSystemNavigationTab"
        Layout.fillWidth: true
        name: "NavigationTab"

        ButtonGroup {
            id: tabGroup
        }

        DesignSystemState {
            label: "checked: true"
            NavigationTab { text: "Activity"; checked: true; ButtonGroup.group: tabGroup }
        }
        DesignSystemState {
            label: "rest"
            NavigationTab { text: "Send"; ButtonGroup.group: tabGroup }
        }
        DesignSystemState {
            label: "rest"
            NavigationTab { text: "Receive"; ButtonGroup.group: tabGroup }
        }
        DesignSystemState {
            label: "iconSource"
            NavigationTab {
                implicitWidth: 60
                iconSource: "image://images/gear"
                iconColor: Theme.color.neutral7
                iconSize: 24
                Accessible.name: "Settings"
                ButtonGroup.group: tabGroup
            }
        }
        DesignSystemState {
            label: "enabled: false"
            NavigationTab { text: "Disabled"; enabled: false }
        }
    }
}

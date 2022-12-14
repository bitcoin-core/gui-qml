// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BitcoinApp.Controls
import BitcoinApp.Components

Item {
    id: nodeSettings
    property alias navMiddleDetail: nodeSettingsView.navMiddleDetail
    property alias navRightDetail: nodeSettingsView.navRightDetail

    StackView {
        id: nodeSettingsView
        property alias navMiddleDetail: node_settings.navMiddleDetail
        property alias navRightDetail: node_settings.navRightDetail
        anchors.fill: parent

        initialItem: Page {
            id: node_settings
            property alias navMiddleDetail: navbar.middleDetail
            property alias navRightDetail: navbar.rightDetail
            background: null
            implicitWidth: 450
            leftPadding: 20
            rightPadding: 20
            topPadding: 30

            header: NavigationBar {
                id: navbar
            }
            ColumnLayout {
                spacing: 20
                width: Math.min(parent.width, 450)
                anchors.horizontalCenter: parent.horizontalCenter
                Setting {
                    Layout.fillWidth: true
                    header: qsTr("Dark Mode")
                    actionItem: OptionSwitch {
                        checked: Theme.dark
                        onToggled: Theme.toggleDark()
                    }
                }
                Setting {
                    Layout.fillWidth: true
                    header: qsTr("About")
                    actionItem: NavButton {
                        iconSource: "image://images/caret-right"
                        background: null
                        onClicked: {
                            nodeSettingsView.push(about_page)
                        }
                    }
                }
                Setting {
                    Layout.fillWidth: true
                    header: qsTr("Storage")
                    actionItem: NavButton {
                        iconSource: "image://images/caret-right"
                        background: null
                        onClicked: {
                            nodeSettingsView.push(storage_page)
                        }
                    }
                }
                Setting {
                    Layout.fillWidth: true
                    header: qsTr("Connection")
                    actionItem: NavButton {
                        iconSource: "image://images/caret-right"
                        background: null
                        onClicked: {
                            nodeSettingsView.push(connection_page)
                        }
                    }
                }
            }
        }
    }
    Component {
        id: about_page
        SettingsAbout {
            navLeftDetail: NavButton {
                iconSource: "image://images/caret-left"
                text: qsTr("Back")
                onClicked: {
                    nodeSettingsView.pop()
                }
            }
        }
    }
    Component {
        id: storage_page
        SettingsStorage {
            navLeftDetail: NavButton {
                iconSource: "image://images/caret-left"
                text: qsTr("Back")
                onClicked: {
                    nodeSettingsView.pop()
                }
            }
        }
    }
    Component {
        id: connection_page
        SettingsConnection {
            navLeftDetail: NavButton {
                iconSource: "image://images/caret-left"
                text: qsTr("Back")
                onClicked: {
                    nodeSettingsView.pop()
                }
            }
        }
    }
}

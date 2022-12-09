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
        initialItem: Page {
            id: node_settings
            property alias navMiddleDetail: navbar.middleDetail
            property alias navRightDetail: navbar.rightDetail
            background: null
            header: NavigationBar {
                id: navbar
            }
            ColumnLayout {
                spacing: 0
                width: parent.width
                ColumnLayout {
                    spacing: 20
                    Layout.maximumWidth: 450
                    Layout.topMargin: 30
                    Layout.leftMargin: 20
                    Layout.rightMargin: 20
                    Layout.alignment: Qt.AlignCenter
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
        anchors.fill: parent
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

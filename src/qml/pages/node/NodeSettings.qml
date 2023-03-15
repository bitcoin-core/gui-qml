// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../../controls"
import "../../components"
import "../settings"

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
                spacing: 4
                width: Math.min(parent.width, 450)
                anchors.horizontalCenter: parent.horizontalCenter
                Setting {
                    Layout.fillWidth: true
                    header: qsTr("Dark Mode")
                    actionItem: OptionSwitch {
                        checked: Theme.dark
                        onToggled: Theme.toggleDark()
                    }
                    onClicked: loadedItem.toggled()
                }
                Separator { Layout.fillWidth: true }
                Setting {
                    id: gotoAbout
                    Layout.fillWidth: true
                    header: qsTr("About")
                    actionItem: CaretRightButton {
                        stateColor: gotoAbout.stateColor
                        onClicked: {
                            nodeSettingsView.push(about_page)
                        }
                    }
                    onClicked: loadedItem.clicked()
                }
                Separator { Layout.fillWidth: true }
                Setting {
                    id: gotoStorage
                    Layout.fillWidth: true
                    header: qsTr("Storage")
                    actionItem: CaretRightButton {
                        stateColor: gotoStorage.stateColor
                        onClicked: {
                            nodeSettingsView.push(storage_page)
                        }
                    }
                    onClicked: loadedItem.clicked()
                }
                Separator { Layout.fillWidth: true }
                Setting {
                    id: gotoConnection
                    Layout.fillWidth: true
                    header: qsTr("Connection")
                    actionItem: CaretRightButton {
                        stateColor: gotoConnection.stateColor
                        onClicked: {
                            nodeSettingsView.push(connection_page)
                        }
                    }
                    onClicked: loadedItem.clicked()
                }
                Separator { Layout.fillWidth: true }
                Setting {
                    id: gotoPeers
                    Layout.fillWidth: true
                    header: qsTr("Peers")
                    actionItem: CaretRightButton {
                        stateColor: gotoPeers.stateColor
                        onClicked: {
                            peerTableModel.startAutoRefresh();
                            nodeSettingsView.push(peers_page)
                        }
                    }
                    onClicked: loadedItem.clicked()
                }
                Separator { Layout.fillWidth: true }
                Setting {
                    id: gotoNetworkTraffic
                    Layout.fillWidth: true
                    header: qsTr("Network Traffic")
                    actionItem: CaretRightButton {
                        stateColor: gotoNetworkTraffic.stateColor
                        onClicked: {
                            nodeSettingsView.push(networktraffic_page)
                        }
                    }
                    onClicked: loadedItem.clicked()
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
    Component {
        id: peers_page
        Peers {
            navLeftDetail: NavButton {
                iconSource: "image://images/caret-left"
                text: qsTr("Back")
                onClicked: {
                    nodeSettingsView.pop()
                    peerTableModel.stopAutoRefresh();
                }
            }
        }
    }
    Component {
        id: networktraffic_page
        NetworkTraffic {
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

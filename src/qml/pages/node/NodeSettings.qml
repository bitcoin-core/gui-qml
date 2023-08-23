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
                    id: gotoAbout
                    Layout.fillWidth: true
                    header: qsTr("About")
                    actionItem: CaretRightButton {
                        color: gotoAbout.stateColor
                    }
                    onClicked: {
                        nodeSettingsView.push(about_page)
                    }
                }
                Separator { Layout.fillWidth: true }
                Setting {
                    id: gotoDisplay
                    Layout.fillWidth: true
                    header: qsTr("Display")
                    actionItem: CaretRightButton {
                        color: gotoDisplay.stateColor
                    }
                    onClicked: {
                        nodeSettingsView.push(display_page)
                    }
                }
                Separator { Layout.fillWidth: true }
                Setting {
                    id: gotoStorage
                    Layout.fillWidth: true
                    header: qsTr("Storage")
                    actionItem: CaretRightButton {
                        color: gotoStorage.stateColor
                    }
                    onClicked: {
                        nodeSettingsView.push(storage_page)
                    }
                }
                Separator { Layout.fillWidth: true }
                Setting {
                    id: gotoConnection
                    Layout.fillWidth: true
                    header: qsTr("Connection")
                    actionItem: CaretRightButton {
                        color: gotoConnection.stateColor
                    }
                    onClicked: {
                        nodeSettingsView.push(connection_page)
                    }
                }
                Separator { Layout.fillWidth: true }
                Setting {
                    id: gotoPeers
                    Layout.fillWidth: true
                    header: qsTr("Peers")
                    actionItem: CaretRightButton {
                        color: gotoPeers.stateColor
                    }
                    onClicked: {
                        peerTableModel.startAutoRefresh();
                        nodeSettingsView.push(peers_page)
                    }
                }
                Separator { Layout.fillWidth: true }
                Setting {
                    id: gotoNetworkTraffic
                    Layout.fillWidth: true
                    header: qsTr("Network Traffic")
                    actionItem: CaretRightButton {
                        color: gotoNetworkTraffic.stateColor
                    }
                    onClicked: {
                        nodeSettingsView.push(networktraffic_page)
                    }
                }
            }
        }
    }
    Component {
        id: about_page
        SettingsAbout {
            showHeader: false
            navLeftDetail: NavButton {
                iconSource: "image://images/caret-left"
                text: qsTr("Back")
                onClicked: {
                    nodeSettingsView.pop()
                }
            }
            navMiddleDetail: Header {
                headerBold: true
                headerSize: 18
                header: qsTr("About")
            }
            devMiddleDetail: Header {
                headerBold: true
                headerSize: 18
                header: qsTr("Developer settings")
            }
        }
    }
    Component {
        id: display_page
        SettingsDisplay {
            navLeftDetail: NavButton {
                iconSource: "image://images/caret-left"
                text: qsTr("Back")
                onClicked: {
                    nodeSettingsView.pop()
                }
            }
            navMiddleDetail: Header {
                headerBold: true
                headerSize: 18
                header: qsTr("Display settings")
            }
        }
    }
    Component {
        id: storage_page
        SettingsStorage {
            showHeader: false
            navLeftDetail: NavButton {
                iconSource: "image://images/caret-left"
                text: qsTr("Back")
                onClicked: {
                    nodeSettingsView.pop()
                }
            }
            navMiddleDetail: Header {
                headerBold: true
                headerSize: 18
                header: qsTr("Storage settings")
            }
        }
    }
    Component {
        id: connection_page
        SettingsConnection {
            showHeader: false
            navLeftDetail: NavButton {
                iconSource: "image://images/caret-left"
                text: qsTr("Back")
                onClicked: {
                    nodeSettingsView.pop()
                }
            }
            navMiddleDetail: Header {
                headerBold: true
                headerSize: 18
                header: qsTr("Connection settings")
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
            navMiddleDetail: Header {
                headerBold: true
                headerSize: 18
                header: qsTr("Peers")
            }
        }
    }
    Component {
        id: networktraffic_page
        NetworkTraffic {
            showHeader: false
            navLeftDetail: NavButton {
                iconSource: "image://images/caret-left"
                text: qsTr("Back")
                onClicked: {
                    nodeSettingsView.pop()
                }
            }
            navMiddleDetail: Header {
                headerBold: true
                headerSize: 18
                header: qsTr("Network traffic")
            }
        }
    }
}

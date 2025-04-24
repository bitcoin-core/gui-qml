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
    signal doneClicked

    id: root

    StackView {
        id: nodeSettingsView
        anchors.fill: parent

        initialItem: Page {
            id: node_settings
            background: null
            implicitWidth: 450
            leftPadding: 20
            rightPadding: 20
            topPadding: 30

            header: NavigationBar2 {
                centerItem: Header {
                    headerBold: true
                    headerSize: 18
                    header: "Settings"
                }
                rightItem: NavButton {
                    text: qsTr("Done")
                    onClicked: root.doneClicked()
                }
            }
            ColumnLayout {
                spacing: 4
                width: Math.min(parent.width, 450)
                anchors.horizontalCenter: parent.horizontalCenter
                Setting {
                    id: gotoAbout
                    Layout.fillWidth: true
                    header: qsTr("About")
                    actionItem: CaretRightIcon {
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
                    actionItem: CaretRightIcon {
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
                    actionItem: CaretRightIcon {
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
                    actionItem: CaretRightIcon {
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
                    actionItem: CaretRightIcon {
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
                    actionItem: CaretRightIcon {
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
            onBackClicked: {
                nodeSettingsView.pop()
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
            onBackClicked: {
                nodeSettingsView.pop()
                peerTableModel.stopAutoRefresh();
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

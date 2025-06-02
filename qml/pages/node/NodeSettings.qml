// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../../controls"
import "../../components"
import "../settings"

PageStack {
    signal doneClicked

    property alias showDoneButton: doneButton.visible

    id: root

    initialItem: Page {
        background: null
        header: NavigationBar2 {
            centerItem: Header {
                headerBold: true
                headerSize: 18
                header: "Settings"
            }
            rightItem: NavButton {
                id: doneButton
                text: qsTr("Done")
                onClicked: root.doneClicked()
            }
        }
        contentItem: RowLayout {
            ColumnLayout {
                Layout.alignment: Qt.AlignHCenter | Qt.AlignTop
                Layout.fillHeight: false
                Layout.fillWidth: true
                Layout.margins: 20
                Layout.maximumWidth: 450
                spacing: 4
                Setting {
                    id: gotoAbout
                    Layout.fillWidth: true
                    header: qsTr("About")
                    actionItem: CaretRightIcon {
                        color: gotoAbout.stateColor
                    }
                    onClicked: {
                        root.push(about_page)
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
                        root.push(display_page)
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
                        root.push(storage_page)
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
                        root.push(connection_page)
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
                        root.push(peers_page)
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
                        root.push(networktraffic_page)
                    }
                }
                Item {
                    Layout.fillHeight: true
                }
            }
        }
    }

    Component {
        id: about_page
        SettingsAbout {
            onBack: root.pop()
        }
    }
    Component {
        id: display_page
        SettingsDisplay {
            onBack: {
                root.pop()
            }
        }
    }
    Component {
        id: storage_page
        SettingsStorage {
            onBack: root.pop()
        }
    }
    Component {
        id: connection_page
        SettingsConnection {
            onBack: root.pop()
        }
    }
    Component {
        id: peers_page
        Peers {
            onBack: {
                root.pop()
                peerTableModel.stopAutoRefresh();
            }
            onPeerSelected: (peerDetails) => {
                root.push(peer_details, {"details": peerDetails})
            }
        }
    }
    Component {
        id: peer_details
        PeerDetails {
            onBack: {
                root.pop()
            }
        }
    }
    Component {
        id: networktraffic_page
        NetworkTraffic {
            showHeader: false
            onBack: root.pop()
        }
    }
}

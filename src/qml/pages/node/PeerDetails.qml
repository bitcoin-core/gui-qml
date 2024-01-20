// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0
import "../../controls"
import "../../components"

Page {
    id: root
    signal backClicked()

    property PeerDetailsModel details

    Connections {
        target: details
        function onDisconnected() {
            root.backClicked()
        }
    }

    background: null
    header: NavigationBar2 {
        leftItem: NavButton {
            iconSource: "image://images/caret-left"
            text: qsTr("Back")
            onClicked: root.backClicked()
        }
        centerItem: Header {
            headerBold: true
            headerSize: 18
            header: qsTr("Peer " + details.nodeId)
        }
    }

    ScrollView {
        id: scrollView
        width: parent.width
        height: parent.height
        clip: true
        contentWidth: width

        Column {
            width: Math.min(parent.width - 40, 450)
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 10
            topPadding: 30
            bottomPadding: 30

            CoreText {
                text: qsTr("Information");
                bold: true;
                font.pixelSize: 18;
                horizontalAlignment: Qt.AlignLeft;
                color: Theme.color.neutral9;
            }

            Column {
                width: parent.width
                bottomPadding: 5

                KeyValueRow { key: KeyText { text: qsTr("Address"); } value: ValText { text: details.address; color: Theme.color.neutral9; }}
                KeyValueRow { key: KeyText { text: qsTr("VIA"); } value: ValText { text: details.addressLocal; color: Theme.color.neutral9; }}
                KeyValueRow { key: KeyText { text: qsTr("Type"); } value: ValText { text: details.type; color: Theme.color.neutral9; }}
                KeyValueRow {
                    id: permissionsRow
                    property string permissionsValue: details.permission
                    property bool isPermissioned: permissionsValue != "N/A"
                    key: KeyText {
                        text: qsTr("Permissions");
                        active: permissionsRow.isPermissioned
                    }
                    value: Loader {
                        sourceComponent: permissionsRow.isPermissioned ? permissioned : notPermissioned
                    }
                    Component {
                        id: permissioned
                        ValText { text: permissionsRow.permissionsValue; }
                    }
                    Component {
                        id: notPermissioned
                        Row {
                            IconButton {
                                iconLocation: "image://images/minus";
                                icon.color: Theme.color.neutral6
                            }
                        }
                    }
                }
                KeyValueRow { key: KeyText { text: qsTr("Version"); } value: ValText { text: details.version; }}
                KeyValueRow { key: KeyText { text: qsTr("User agent"); } value: ValText { text: details.userAgent; }}
                KeyValueRow { key: KeyText { text: qsTr("Services"); } value: ValText { text: details.services; }}
                KeyValueRow {
                    id: transactionRelayRow
                    property bool isTransactionRelay: details.transactionRelay
                    key: KeyText { text: qsTr("Transaction relay"); }
                    value: Row {
                        IconButton {
                            iconLocation: transactionRelayRow.isTransactionRelay ? "image://images/check" : "image://images/cross"
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                }
                KeyValueRow {
                    id: addressRelayRow
                    property bool isAddressRelay: details.addressRelay
                    key: KeyText { text: qsTr("Address relay"); }
                    value: Row {
                        IconButton {
                            iconLocation: addressRelayRow.isAddressRelay ? "image://images/check" : "image://images/cross"
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                }
                KeyValueRow {
                    id: asRow
                    property string mappedASValue: details.mappedAS
                    property bool isMappedAS: mappedASValue != "N/A"
                    key: KeyText {
                        text: qsTr("Mapped AS");
                        active: asRow.isMappedAS
                    }

                    value: Loader {
                        sourceComponent: asRow.isMappedAS ? mappedAs : notMappedAs
                    }

                    Component {
                        id: mappedAs
                        ValText { text: asRow.mappedASValue; }
                    }

                    Component {
                        id: notMappedAs
                        Row {
                            IconButton {
                                iconLocation: "image://images/minus";
                                icon.color: Theme.color.neutral6
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }
                    }
                }
            }

            CoreText {
                text: qsTr("Block data");
                bold: true;
                font.pixelSize: 18;
                horizontalAlignment: Qt.AlignLeft;
                color: Theme.color.neutral9;
            }

            Column {
                width: parent.width
                bottomPadding: 5
                KeyValueRow { key: KeyText { text: qsTr("Starting block"); } value: ValText { text: details.startingHeight; }}
                KeyValueRow { key: KeyText { text: qsTr("Synced headers"); } value: ValText { text: details.syncedHeaders; }}
                KeyValueRow { key: KeyText { text: qsTr("Synced blocks"); } value: ValText { text: details.syncedBlocks; }}
            }

            CoreText {
                text: qsTr("Network traffic");
                bold: true;
                font.pixelSize: 18;
                horizontalAlignment: Qt.AlignLeft;
                color: Theme.color.neutral9;
            }
            Column {
                width: parent.width
                bottomPadding: 5
                KeyValueRow {
                    key: KeyText { text: qsTr("Direction"); }
                    value: Row {
                        IconButton {
                            iconLocation: details.direction === "Inbound" ? "image://images/arrow-down" : "image://images/arrow-up"
                            icon.height: 9
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        ValText {
                            text: details.direction
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                }
                KeyValueRow { key: KeyText { text: qsTr("Connection time"); } value: NetStatValue { text: details.connectionDuration; }}
                KeyValueRow { key: KeyText { text: qsTr("Last send"); } value: NetStatValue { text: details.lastSend + qsTr(" ago"); }}
                KeyValueRow { key: KeyText { text: qsTr("Last receive"); } value: NetStatValue { text: details.lastReceived + qsTr(" ago"); }}
                KeyValueRow { key: KeyText { text: qsTr("Sent"); } value: NetStatValue { text: details.bytesSent + qsTr(" total"); }}
                KeyValueRow { key: KeyText { text: qsTr("Received"); } value: NetStatValue { text: details.bytesReceived + qsTr(" total"); }}
                KeyValueRow { key: KeyText { text: qsTr("Ping time"); } value: NetStatValue { text: details.pingTime; }}
                KeyValueRow {
                    id: pingWaitRow
                    property string pingWaitValue: details.pingWait
                    property bool isPingWait: pingWaitValue != "N/A"
                    key: KeyText {
                        text: qsTr("Ping wait");
                        active: pingWaitRow.isPingWait
                    }

                    value: Loader {
                        sourceComponent: pingWaitRow.isPingWait ? pingWait : notPingWait
                    }

                    Component {
                        id: pingWait
                        ValText { text: pingWaitRow.pingWaitValue; }
                    }

                    Component {
                        id: notPingWait
                        Row {
                            Button {
                                padding: 0
                                display: AbstractButton.IconOnly
                                height: 21
                                width: 21
                                icon.source: "image://images/minus"
                                icon.color: Theme.color.neutral6
                                icon.height: 21
                                icon.width: 21
                                background: null
                            }
                        }
                    }
                }
                KeyValueRow { key: KeyText { text: qsTr("Min ping"); } value: NetStatValue { text: details.pingMin; }}
                KeyValueRow { key: KeyText {text: qsTr("Time offset"); } value: NetStatValue { text: details.timeOffset; }}
            }
        }
    }

    component KeyText: CoreText {
        property bool active: true
        color: active ? Theme.color.neutral9 : Theme.color.neutral6
        horizontalAlignment: Qt.AlignLeft
        verticalAlignment: Text.AlignVCenter
    }

    component ValText: CoreText {
        property bool active: true
        color: active ? Theme.color.neutral8 : Theme.color.neutral6
        horizontalAlignment: Qt.AlignLeft
        verticalAlignment: Text.AlignVCenter
    }

    component IconButton: Button {
        id: iconButton
        property alias iconLocation: iconButton.icon.source
        padding: 0
        display: AbstractButton.IconOnly
        height: 21
        width: 21
        icon.color: Theme.color.neutral9
        icon.height: 21
        icon.width: 21
        background: null
    }

    component NetStatIndicator: Button {
        width: 21
        height: 21
        background: Rectangle {
            width: 8
            height: 8
            radius: 4
            anchors.centerIn: parent
            color: Theme.color.green
        }
    }

    component NetStatValue: Row {
        property alias text: valText.text
        spacing: 0
        NetStatIndicator {}
        ValText {
            id: valText
            anchors.verticalCenter: parent.verticalCenter
        }
    }
}



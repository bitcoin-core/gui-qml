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
    objectName: "peerDetails"
    signal back()

    property PeerDetailsModel details

    function showActionError(message) {
        peerActionError.message = message
        peerActionError.open()
    }

    Connections {
        target: details
        function onDisconnected() {
            root.back()
        }
    }

    background: null
    header: NavigationBar2 {
        leftItem: NavButton {
            iconSource: "image://images/caret-left"
            text: qsTr("Back")
            onClicked: root.back()
        }
        centerItem: Header {
            headerBold: true
            headerSize: 18
            header: qsTr("Peer %1").arg(details.nodeId)
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

            RowLayout {
                width: parent.width
                spacing: 10

                OutlineButton {
                    objectName: "peerDisconnectButton"
                    enabled: details.connected
                    Layout.fillWidth: true
                    Layout.preferredWidth: 0
                    text: qsTr("Disconnect")
                    bold: false
                    onClicked: {
                        if (!peerTableModel.disconnectPeer(details.nodeId)) {
                            root.showActionError(qsTr("Could not disconnect peer. The peer may already be disconnected or the node state may have changed."))
                        }
                    }
                }

                OutlineButton {
                    objectName: "peerBanButton"
                    enabled: details.connected
                    Layout.fillWidth: true
                    Layout.preferredWidth: 0
                    text: qsTr("Ban")
                    bold: false
                    onClicked: banPopup.open()
                }
            }
        }
    }

    Popup {
        id: banPopup
        objectName: "banPopup"
        anchors.centerIn: parent
        modal: true
        padding: 20
        width: Math.min(root.width - 40, 350)
        background: Rectangle {
            color: Theme.color.background
            radius: 8
            border.color: Theme.color.neutral3
            border.width: 1
        }

        property int selectedDuration: 3600

        readonly property var durations: [
            { label: qsTr("1 hour"),  secs: 3600 },
            { label: qsTr("1 day"),   secs: 86400 },
            { label: qsTr("1 week"),  secs: 604800 },
            { label: qsTr("1 year"),  secs: 31536000 }
        ]

        ColumnLayout {
            width: parent.width
            spacing: 0

            CoreText {
                Layout.fillWidth: true
                Layout.bottomMargin: 16
                text: qsTr("Ban this peer")
                bold: true
                font.pixelSize: 18
                color: Theme.color.neutral9
                horizontalAlignment: Qt.AlignHCenter
            }

            Repeater {
                model: banPopup.durations
                delegate: Column {
                    Layout.fillWidth: true

                    Separator { width: parent.width }

                    ItemDelegate {
                        id: durationRow
                        objectName: "banDurationRow_" + modelData.secs
                        width: parent.width
                        leftPadding: 8
                        rightPadding: 8
                        hoverEnabled: AppMode.isDesktop
                        background: null
                        contentItem: RowLayout {
                            CoreText {
                                Layout.fillWidth: true
                                text: modelData.label
                                font.pixelSize: 16
                                color: durationRow.hovered ? Theme.color.orangeLight1 : Theme.color.neutral9
                                horizontalAlignment: Text.AlignLeft
                                verticalAlignment: Text.AlignVCenter
                            }
                            IconButton {
                                opacity: banPopup.selectedDuration === modelData.secs ? 1 : 0
                                iconLocation: "image://images/check"
                                icon.color: durationRow.hovered ? Theme.color.orangeLight1 : Theme.color.neutral9
                            }
                        }
                        onClicked: banPopup.selectedDuration = modelData.secs
                    }
                }
            }

            Separator { Layout.fillWidth: true; Layout.bottomMargin: 16 }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                OutlineButton {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 0
                    text: qsTr("Cancel")
                    onClicked: banPopup.close()
                }

                ContinueButton {
                    objectName: "banConfirmButton"
                    enabled: details.connected
                    Layout.fillWidth: true
                    Layout.preferredWidth: 0
                    text: qsTr("Ban")
                    onClicked: {
                        banPopup.close()
                        if (!peerTableModel.banPeer(details.rawAddress, banPopup.selectedDuration)) {
                            root.showActionError(qsTr("Could not ban peer. The peer may already be disconnected or the node state may have changed."))
                        }
                    }
                }
            }
        }
    }

    AlertPopup {
        id: peerActionError
        objectName: "peerActionErrorPopup"
        title: qsTr("Peer action failed")
        messageObjectName: "actionErrorMessage"

        AlertAction {
            text: qsTr("OK")
            buttonObjectName: "actionErrorCloseButton"
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

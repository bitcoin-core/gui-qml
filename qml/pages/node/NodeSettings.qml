// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0
import "../../controls"
import "../../components"
import "../wallet"
import "../settings"

PageStack {
    signal doneClicked
    signal selectWalletRequested
    signal receiveRequested

    property alias showDoneButton: doneButton.visible
    property bool closingWalletSettingsSubpage: false

    id: root
    objectName: "nodeSettingsStack"

    function isWalletSettingsSubpage(page_name) {
        return page_name === "walletPasswordSettingsPage"
            || page_name === "addressListPage"
            || page_name === "signVerifyMessagePage"
    }

    function closeWalletSettingsSubpage() {
        const current_name = root.currentItem && root.currentItem.objectName ? root.currentItem.objectName : ""
        if (root.closingWalletSettingsSubpage || root.depth <= 1 || !isWalletSettingsSubpage(current_name)) {
            return
        }
        root.closingWalletSettingsSubpage = true
        root.pop()
        Qt.callLater(function() {
            root.closingWalletSettingsSubpage = false
        })
    }

    function openWalletSettings() {
        while (root.depth > 1) {
            root.pop()
        }
        const current_name = root.currentItem && root.currentItem.objectName ? root.currentItem.objectName : ""
        if (current_name !== "walletSettingsPage") {
            root.push(wallet_settings_page)
        }
    }

    Connections {
        target: typeof walletController !== "undefined" ? walletController : null
        function onOpenWalletSettingsRequested() {
            root.openWalletSettings()
        }
        function onSelectedWalletChanged() {
            root.closeWalletSettingsSubpage()
        }
        function onIsWalletLoadedChanged() {
            if (!walletController.isWalletLoaded) {
                root.closeWalletSettingsSubpage()
            }
        }
    }
    initialItem: Page {
        background: null
        header: NavigationBar2 {
            centerItem: Header {
                headerBold: true
                headerSize: 18
                header: qsTr("Settings")
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
                    id: gotoDisplay
                    objectName: "gotoDisplay"
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
                    id: gotoWallet
                    objectName: "settingsWallet"
                    Layout.fillWidth: true
                    header: qsTr("Wallet")
                    actionItem: CaretRightIcon {
                        color: gotoWallet.stateColor
                    }
                    onClicked: {
                        root.openWalletSettings()
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
                    id: gotoExternalSigner
                    objectName: "settingsExternalSigner"
                    visible: AppMode.walletEnabled
                    Layout.fillWidth: true
                    header: qsTr("External Signer")
                    actionItem: CaretRightIcon {
                        color: gotoExternalSigner.stateColor
                    }
                    onClicked: {
                        root.push(wallet_page)
                    }
                }
                Separator {
                    visible: gotoExternalSigner.visible
                    Layout.fillWidth: true
                }
                Setting {
                    id: gotoConnection
                    objectName: "settingsConnection"
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
                    objectName: "settingsPeers"
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
                Separator { Layout.fillWidth: true }
                Setting {
                    id: gotoMempoolInformation
                    objectName: "settingsMempoolInformation"
                    Layout.fillWidth: true
                    header: qsTr("Mempool Information")
                    actionItem: CaretRightIcon {
                        color: gotoMempoolInformation.stateColor
                    }
                    onClicked: {
                        root.push(mempool_information_page)
                    }
                }
                Separator { Layout.fillWidth: true }
                Setting {
                    id: gotoDebugLog
                    objectName: "settingsDebugLog"
                    Layout.fillWidth: true
                    header: qsTr("Debug Log")
                    actionItem: CaretRightIcon {
                        color: gotoDebugLog.stateColor
                    }
                    onClicked: {
                        root.push(debug_log_page)
                    }
                }
                Separator { Layout.fillWidth: true }
                Setting {
                    id: gotoAbout
                    objectName: "gotoAboutSetting"
                    Layout.fillWidth: true
                    header: qsTr("About")
                    actionItem: CaretRightIcon {
                        color: gotoAbout.stateColor
                    }
                    onClicked: {
                        root.push(about_page)
                    }
                }
                Separator {
                    Layout.fillWidth: true
                    visible: AppMode.isDesktop
                }
                Setting {
                    id: gotoWindowBehavior
                    objectName: "settingsWindowBehavior"
                    visible: AppMode.isDesktop
                    Layout.fillWidth: true
                    header: qsTr("Window Behavior")
                    actionItem: CaretRightIcon {
                        color: gotoWindowBehavior.stateColor
                    }
                    onClicked: {
                        root.push(window_behavior_page)
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
        id: wallet_page
        SettingsWallet {
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
        id: addresses_page
        AddressList {
            onBack: root.pop()
            onReceiveRequested: {
                root.pop()
                root.receiveRequested()
            }
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
            onBannedPeers: {
                root.push(banned_peers_page)
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
        id: banned_peers_page
        BannedPeers {
            onBack: root.pop()
        }
    }
    Component {
        id: networktraffic_page
        NetworkTraffic {
            showHeader: false
            onBack: root.pop()
        }
    }
    Component {
        id: mempool_information_page
        MempoolInformationSettings {
            onBack: root.pop()
        }
    }
    Component {
        id: debug_log_page
        SettingsDebugLog {
            onBack: root.pop()
        }
    }
    Component {
        id: wallet_settings_page
        WalletSettings {
            onBack: root.pop()
            onSelectWalletRequested: root.selectWalletRequested()
            onPasswordRequested: root.push(wallet_password_page, { "updating": walletController.selectedWallet.isEncrypted })
            onSignVerifyMessageRequested: root.push(sign_verify_message_page)
            onAddressesRequested: {
                walletController.selectedWallet.addressListModel.refresh()
                root.push(addresses_page)
            }
        }
    }
    Component {
        id: sign_verify_message_page
        SignVerifyMessage {
            onBack: root.pop()
        }
    }
    Component {
        id: wallet_password_page
        WalletPasswordSettings {
            onBack: root.pop()
            onSaved: root.pop()
        }
    }
    Component {
        id: window_behavior_page
        SettingsWindowBehavior {
            onBack: root.pop()
        }
    }
}

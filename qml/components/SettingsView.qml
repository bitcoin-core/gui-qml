pragma ComponentBehavior: Bound

// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import org.bitcoincore.qt 1.0

import "../controls"
import "../pages/settings" as SettingsPages
import "../pages/wallet" as WalletPages

Page {
    id: root
    objectName: "settingsView"

    signal doneClicked()
    signal selectWalletRequested()
    signal receiveRequested()

    property bool showDoneButton: true
    property string selectedSectionId: ""
    property int sidebarWidth: 286
    readonly property alias sidebar: sidebar
    readonly property alias pageContainer: pageContainer
    readonly property var groupTitles: ({
        "wallet": qsTr("Wallet"),
        "general": qsTr("General"),
        "network": qsTr("Network"),
        "advanced": qsTr("Advanced")
    })
    readonly property var sections: [
        {
            id: "wallet",
            label: qsTr("Wallet settings"),
            group: "wallet",
            visible: AppMode.walletEnabled,
            pageComponent: walletPage
        },
        {
            id: "external-signer",
            label: qsTr("External signer"),
            group: "wallet",
            visible: AppMode.walletEnabled,
            pageComponent: externalSignerPage
        },
        {
            id: "display",
            label: qsTr("Display"),
            group: "general",
            pageComponent: displayPage
        },
        {
            id: "window-behavior",
            label: qsTr("Window behavior"),
            group: "general",
            visible: AppMode.isDesktop,
            pageComponent: windowBehaviorPage
        },
        {
            id: "storage",
            label: qsTr("Storage"),
            group: "general",
            pageComponent: storagePage
        },
        {
            id: "connection",
            label: qsTr("Connection"),
            group: "network",
            pageComponent: connectionPage
        },
        {
            id: "network-traffic",
            label: qsTr("Network traffic"),
            group: "network",
            pageComponent: networkTrafficPage
        },
        {
            id: "mempool",
            label: qsTr("Mempool information"),
            group: "advanced",
            visible: nodeModel.mempoolInformationAvailable,
            pageComponent: mempoolPage
        },
        {
            id: "rpc-console",
            label: qsTr("RPC console"),
            group: "advanced",
            visible: AppMode.isDesktop,
            pageComponent: rpcConsolePage
        },
        {
            id: "debug-log",
            label: qsTr("Debug log"),
            group: "advanced",
            pageComponent: debugLogPage
        },
        {
            id: "about",
            label: qsTr("About"),
            group: "about",
            pageComponent: aboutPage
        }
    ]

    function sectionForId(sectionId) {
        for (let index = 0; index < root.sections.length; ++index) {
            if (root.sections[index].id === sectionId) return root.sections[index]
        }
        return null
    }

    function componentForSection(sectionId) {
        const section = root.sectionForId(sectionId)
        return section ? section.pageComponent : null
    }

    function sectionIsVisible(sectionId) {
        const section = root.sectionForId(sectionId)
        return section !== null && section.visible !== false
    }

    function firstVisibleSectionId() {
        for (let index = 0; index < root.sections.length; ++index) {
            if (root.sections[index].visible !== false) return root.sections[index].id
        }
        return ""
    }

    function selectSection(sectionId, forceReload) {
        const resolvedId = root.sectionIsVisible(sectionId) ? sectionId : root.firstVisibleSectionId()
        if (resolvedId.length === 0) {
            root.selectedSectionId = ""
            pageContainer.clear()
            return
        }

        if (forceReload === true) pageContainer.clear()
        root.selectedSectionId = resolvedId
        pageContainer.showSection(resolvedId, root.componentForSection(resolvedId))
    }

    function ensureVisibleSelection() {
        if (!root.sectionIsVisible(root.selectedSectionId)) root.selectSection(root.firstVisibleSectionId())
    }

    function openWalletSettings() {
        root.selectSection("wallet")
    }

    function openWalletAddressHistory() {
        if (!walletController.isWalletLoaded || !walletController.selectedWallet) return
        walletController.selectedWallet.addressListModel.refresh()
        root.selectSection("wallet", true)
        pageContainer.push(addressListPage)
    }

    background: null
    padding: 0

    onSectionsChanged: ensureVisibleSelection()
    onVisibleChanged: {
        if (visible) root.selectSection(root.selectedSectionId)
    }

    Component.onCompleted: root.selectSection(
        root.sectionIsVisible(root.selectedSectionId) ? root.selectedSectionId : root.firstVisibleSectionId())

    Connections {
        target: typeof walletController !== "undefined" ? walletController : null

        function onSelectedWalletChanged() {
            if (root.selectedSectionId === "wallet" && pageContainer.depth > 1) root.selectSection("wallet", true)
        }

        function onIsWalletLoadedChanged() {
            if (!walletController.isWalletLoaded && root.selectedSectionId === "wallet" && pageContainer.depth > 1) {
                root.selectSection("wallet", true)
            }
        }
    }

    contentItem: RowLayout {
        spacing: 0

        Rectangle {
            id: sidebarSurface
            objectName: "settingsSidebarSurface"
            Layout.preferredWidth: root.sidebarWidth
            Layout.minimumWidth: root.sidebarWidth
            Layout.maximumWidth: root.sidebarWidth
            Layout.fillHeight: true
            color: Theme.color.neutral1

            Behavior on color {
                ColorAnimation { duration: 150 }
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                anchors.topMargin: 20
                anchors.bottomMargin: 16
                spacing: 0

                CoreText {
                    objectName: "settingsSidebarHeading"
                    Layout.fillWidth: true
                    Layout.leftMargin: 10
                    Layout.rightMargin: 10
                    Layout.bottomMargin: 24
                    text: qsTr("Settings")
                    color: Theme.color.neutral9
                    font: Theme.text.display.font
                    lineHeight: Theme.text.display.lineHeight
                    lineHeightMode: Text.FixedHeight
                    horizontalAlignment: Text.AlignLeft
                }

                SettingsSidebar {
                    id: sidebar
                    objectName: "settingsSidebar"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: root.sections
                    groupTitles: root.groupTitles
                    currentSectionId: root.selectedSectionId
                    onSectionActivated: function(sectionId) { root.selectSection(sectionId) }
                }

                NavButton {
                    objectName: "settingsDoneButton"
                    visible: root.showDoneButton
                    text: qsTr("Done")
                    Layout.alignment: Qt.AlignHCenter
                    Layout.bottomMargin: 20
                    onClicked: root.doneClicked()
                }
            }
        }

        SettingsPageContainer {
            id: pageContainer
            objectName: "settingsPageContainer"
            Layout.minimumWidth: 0
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
    }

    Component {
        id: walletPage

        SettingsPages.WalletSectionPage {
            onSelectWalletRequested: root.selectWalletRequested()
            onPasswordRequested: pageContainer.push(walletPasswordPage, {
                "updating": walletController.selectedWallet.isEncrypted
            })
            onSignVerifyMessageRequested: pageContainer.push(signVerifyPage)
            onAddressesRequested: {
                if (!walletController.isWalletLoaded || !walletController.selectedWallet) return
                walletController.selectedWallet.addressListModel.refresh()
                pageContainer.push(addressListPage)
            }
        }
    }

    Component {
        id: walletPasswordPage

        WalletPages.WalletPasswordSettings {
            onBack: pageContainer.pop()
            onSaved: pageContainer.pop()
        }
    }

    Component {
        id: signVerifyPage

        WalletPages.SignVerifyMessage {
            onBack: pageContainer.pop()
        }
    }

    Component {
        id: addressListPage

        WalletPages.AddressList {
            onBack: pageContainer.pop()
            onReceiveRequested: {
                pageContainer.pop()
                root.receiveRequested()
            }
        }
    }

    Component {
        id: externalSignerPage
        SettingsPages.ExternalSignerSettingsPage {}
    }

    Component {
        id: displayPage
        SettingsPages.DisplaySettingsPage {}
    }

    Component {
        id: windowBehaviorPage
        SettingsPages.WindowBehaviorSettingsPage {}
    }

    Component {
        id: storagePage
        SettingsPages.StorageSettingsPage {}
    }

    Component {
        id: connectionPage
        SettingsPages.ConnectionSettingsPage {}
    }

    Component {
        id: networkTrafficPage

        SettingsPages.NetworkTrafficSettingsPage {}
    }

    Component {
        id: mempoolPage
        SettingsPages.MempoolSettingsPage {}
    }

    Component {
        id: rpcConsolePage

        SettingsPages.RpcConsoleSettingsPage {
            walletName: typeof walletController !== "undefined"
                && walletController.isWalletLoaded && walletController.selectedWallet
                ? walletController.selectedWallet.name
                : ""
        }
    }

    Component {
        id: debugLogPage

        SettingsPages.SettingsDebugLog {
            showBackButton: false
            maximumContentWidth: width
            contentHorizontalPadding: width >= 900 ? 56 : width >= 640 ? 40 : 24
        }
    }

    Component {
        id: aboutPage
        SettingsPages.AboutSettingsPage {}
    }
}

// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import org.bitcoincore.qt 1.0

import "../controls"
import "../pages/settings" as SettingsPages

Page {
    id: root
    objectName: "settingsView"

    signal doneClicked()
    signal selectWalletRequested()
    // The hosted wallet settings stack belongs to the shell, so it resets
    // that stack when the wallet section is re-selected.
    signal walletSettingsResetRequested()

    //! The settings editor and the navigation guard for its unsaved edits.
    readonly property SettingsNavigationController navigation: SettingsNavigationController {
        objectName: "settingsNavigationController"
        source: ApplicationModels.optionsModel
        onNavigate: function(section, done, reload) {
            if (done) root.doneClicked()
            else root.showSection(section, reload)
        }
    }
    property bool showDoneButton: true
    // The active wallet's settings stack is shown in walletSettingsHost when
    // the shell provides one; otherwise the wallet section shows the
    // settings of `wallet`, or its empty state.
    property bool walletSettingsProvided: false
    readonly property Item walletSettingsHost: internal.walletSettingsHost
    property string activeWalletName: wallet !== null ? wallet.name : ""
    property bool activeWalletRpcContext: wallet !== null
    property string activeWalletRpcName: wallet !== null ? wallet.rpcName : activeWalletName
    property bool activeWalletRpcReady: wallet !== null ? wallet.capabilitiesKnown : activeWalletAvailable
    property bool activeWalletAvailable: wallet !== null && wallet.available
    // Without the desktop wallet shell (the node pages used on Android),
    // the wallet section shows its own settings stack for this wallet.
    property var wallet: null
    property string selectedSectionId: ""
    property int sidebarWidth: 286
    readonly property bool optionsEditable: typeof optionsModel === "undefined"
        || (ApplicationModels.optionsModel.settingsReady !== false && ApplicationModels.optionsModel.settingsPending !== true)
    // Compact widths show one column: the sidebar is the root list, and
    // choosing a section slides its page over it until Back.
    readonly property bool compact: splitView.isCompact
    readonly property SettingsSidebar sidebar: splitView.primaryItem ? splitView.primaryItem.sidebar : null // qmllint disable missing-property
    readonly property SettingsPageContainer pageContainer: splitView.detailItem ? splitView.detailItem.pageContainer : null // qmllint disable missing-property
    readonly property bool sectionUsesSettingsEditor: root.selectedSectionId === "storage"
        || root.selectedSectionId === "connection"
        || root.selectedSectionId === "mempool"
        || root.selectedSectionId === "external-signer"
        || (root.selectedSectionId === "about" && root.pageContainer !== null && root.pageContainer.depth > 1)
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
            visible: ApplicationModels.nodeModel.mempoolInformationAvailable,
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

    // Navigation that may leave unsaved settings edits asks first.
    function selectSection(sectionId, forceReload) { root.navigation.request(sectionId, false) }

    // Shows a section without asking; callers have already resolved edits.
    function applySection(sectionId, forceReload) {
        if (!root.pageContainer) return
        const resolvedId = root.sectionIsVisible(sectionId) ? sectionId : root.firstVisibleSectionId()
        if (resolvedId.length === 0) {
            root.selectedSectionId = ""
            root.pageContainer.clear()
            return
        }

        if (forceReload === true) root.pageContainer.clear()
        root.selectedSectionId = resolvedId
        root.pageContainer.showSection(resolvedId, root.componentForSection(resolvedId))
    }

    function showSectionPage(sectionId, forceReload) {
        root.applySection(sectionId, forceReload)
        if (root.selectedSectionId.length > 0) splitView.showDetail()
    }

    function showSection(sectionId, forceReload) {
        root.showSectionPage(sectionId, forceReload)
    }

    function ensureVisibleSelection() {
        if (!root.sectionIsVisible(root.selectedSectionId)) root.applySection(root.firstVisibleSectionId())
    }

    function openWalletSettings() {
        root.navigation.request("wallet", false)
    }

    background: null
    padding: 0

    onSectionsChanged: ensureVisibleSelection()
    onVisibleChanged: {
        if (visible) root.showSection(root.selectedSectionId)
    }

    Component.onCompleted: {
        root.applySection(
            root.sectionIsVisible(root.selectedSectionId) ? root.selectedSectionId : root.firstVisibleSectionId())
        internal.completed = true
    }

    QtObject {
        id: internal
        property bool completed: false
        property Item walletSettingsHost: null
    }

    contentItem: NavigationSplitView {
        id: splitView
        objectName: "settingsSplitView"
        primaryMinimumWidth: root.sidebarWidth
        primaryPreferredWidth: root.sidebarWidth
        primaryMaximumWidth: root.sidebarWidth
        separatorWidth: 0

        // Narrowing the window keeps the page that was on screen.
        onIsCompactChanged: {
            if (isCompact && internal.completed && root.selectedSectionId.length > 0) showDetail()
        }

        primaryComponent: Rectangle {
            objectName: "settingsSidebarSurface"
            readonly property alias sidebar: sidebar
            color: Theme.color.surface1

            Behavior on color {
                enabled: Theme.motion.animated
                ColorAnimation { duration: Theme.motion.normal }
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.space.xl
                anchors.rightMargin: Theme.space.xl
                anchors.topMargin: Theme.space.xl
                anchors.bottomMargin: Theme.space.l
                spacing: 0

                CoreText {
                    objectName: "settingsSidebarHeading"
                    Layout.fillWidth: true
                    Layout.leftMargin: 10
                    Layout.rightMargin: 10
                    Layout.bottomMargin: Theme.space.xxl
                    text: qsTr("Settings")
                    color: Theme.color.neutral9
                    textStyle: Theme.text.display
                    horizontalAlignment: Text.AlignLeft
                }

                SettingsSidebar {
                    id: sidebar
                    objectName: "settingsSidebar"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: root.sections
                    groupTitles: root.groupTitles
                    // The compact list is a menu of destinations, not a selection.
                    currentSectionId: splitView.isCompact ? "" : root.selectedSectionId
                    onSectionActivated: function(sectionId) {
                        // Re-selecting the current section from the sidebar
                        // returns to its root; pushed sub-pages such as the
                        // address list otherwise survive tab round trips.
                        if (sectionId === root.selectedSectionId) {
                            root.pageContainer.resetSection(sectionId)
                            if (sectionId === "wallet") root.walletSettingsResetRequested()
                            root.showSectionPage(sectionId)
                            return
                        }
                        root.navigation.request(sectionId, false)
                    }
                }

                NavButton {
                    objectName: "settingsDoneButton"
                    visible: root.showDoneButton
                    text: qsTr("Done")
                    enabled: root.navigation.doneEnabled
                    Layout.alignment: Qt.AlignHCenter
                    Layout.bottomMargin: Theme.space.xl
                    onClicked: root.navigation.request("", true)
                }
            }
        }

        detailComponent: ColumnLayout {
            readonly property alias pageContainer: pageContainer
            spacing: 0

            SettingsPageContainer {
                id: pageContainer
                objectName: "settingsPageContainer"
                Layout.fillWidth: true
                Layout.fillHeight: true
                disabledSectionIds: root.optionsEditable ? []
                    : ["external-signer", "display", "window-behavior", "storage", "connection"]
                showRootBackButton: splitView.isCompact
                onRootBackRequested: splitView.showPrimary()
            }

            CoreText {
                objectName: "settingsOperationStatus"
                Layout.fillWidth: true
                Layout.fillHeight: false
                visible: text.length > 0
                padding: Theme.space.l
                wrapMode: Text.WordWrap
                text: typeof optionsModel === "undefined" ? ""
                    : ApplicationModels.optionsModel.settingsError || (ApplicationModels.optionsModel.settingsReady === false ? qsTr("Loading settings…")
                    : ApplicationModels.optionsModel.settingsPending ? qsTr("Saving settings…") : "")
                color: ApplicationModels.optionsModel.settingsError ? Theme.color.dangerText : Theme.color.neutral7
                textStyle: Theme.text.caption
            }

            SettingsEditorBar {
                id: settingsEditorBar
                objectName: "settingsEditorBar"
                editor: root.navigation.editor
                visible: settingsEditorBar.hasVisibleContent
                Layout.fillWidth: true
                Layout.fillHeight: false
                Layout.leftMargin: Theme.space.xxl
                Layout.rightMargin: Theme.space.xxl
                Layout.topMargin: settingsEditorBar.hasVisibleContent ? Theme.space.s : 0
                Layout.bottomMargin: settingsEditorBar.hasVisibleContent ? Theme.space.l : 0
                enabled: root.optionsEditable
                showActions: root.sectionUsesSettingsEditor
            }
        }
    }

    Component {
        id: walletPage

        Item {
            id: walletSettingsHost
            objectName: "walletSettingsHost"

            Component.onCompleted: internal.walletSettingsHost = walletSettingsHost

            Loader {
                id: ownWalletSettings
                anchors.fill: parent
                active: AppMode.walletEnabled && !root.walletSettingsProvided
                source: active ? "../pages/wallet/WalletSettingsHost.qml" : ""
                readonly property WalletSettingsPort port: item as WalletSettingsPort
                Binding {
                    target: ownWalletSettings.port
                    property: "wallet"
                    value: root.wallet
                    when: ownWalletSettings.status === Loader.Ready
                }
                Connections {
                    target: ownWalletSettings.port
                    function onSelectWalletRequested() { root.selectWalletRequested() }
                }
            }

            Connections {
                target: root
                function onWalletSettingsResetRequested() {
                    if (ownWalletSettings.port) ownWalletSettings.port.resetRequested()
                }
            }
        }
    }

    Component {
        id: externalSignerPage
        SettingsPages.ExternalSignerSettingsPage { editor: root.navigation.editor }
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
        SettingsPages.EffectiveSettingsPage { editor: root.navigation.editor; settingsEditable: root.optionsEditable; section: "storage" }
    }

    Component {
        id: connectionPage
        SettingsPages.EffectiveSettingsPage { editor: root.navigation.editor; settingsEditable: root.optionsEditable; section: "connection" }
    }

    Component {
        id: networkTrafficPage

        SettingsPages.NetworkTrafficSettingsPage {}
    }

    Component {
        id: mempoolPage
        SettingsPages.EffectiveSettingsPage { editor: root.navigation.editor; settingsEditable: root.optionsEditable; section: "mempool" }
    }

    Component {
        id: rpcConsolePage

        SettingsPages.SettingsRpcConsoleView {
            walletName: root.activeWalletRpcName
            walletContext: root.activeWalletRpcContext
            targetAvailable: !root.activeWalletRpcContext || (root.activeWalletAvailable && root.activeWalletRpcReady)
        }
    }

    Component {
        id: debugLogPage

        SettingsPages.SettingsDebugLogView {}
    }

    Component {
        id: aboutPage
        SettingsPages.AboutSettingsPage { editor: root.navigation.editor }
    }
}

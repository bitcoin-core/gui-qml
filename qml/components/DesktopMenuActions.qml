// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15

QtObject {
    id: root

    property bool walletMode: false
    property bool walletInitialized: false
    property bool walletLoaded: false
    property bool walletBusy: false
    property bool canUndo: false
    property bool canRedo: false
    property bool canCopy: false
    property bool canPaste: false
    property bool navigationEnabled: true
    property bool shuttingDown: false
    property bool isMacOs: false

    readonly property bool commandEnabled: !shuttingDown
    readonly property bool routeEnabled: commandEnabled && navigationEnabled
    readonly property bool walletManagementEnabled: routeEnabled && walletMode && walletInitialized && !walletBusy
    readonly property bool loadedWalletEnabled: walletManagementEnabled && walletLoaded

    readonly property string fileMenuTitle: qsTr("&File")
    readonly property string editMenuTitle: qsTr("&Edit")
    readonly property string viewMenuTitle: qsTr("&View")
    readonly property string windowMenuTitle: qsTr("&Window")
    readonly property string helpMenuTitle: qsTr("&Help")

    signal createWalletRequested()
    signal closeWalletRequested()
    signal backupWalletRequested()
    signal openUriRequested()
    signal signMessageRequested()
    signal verifyMessageRequested()
    signal loadPsbtRequested()
    signal exitRequested()
    signal settingsRequested()
    signal undoRequested()
    signal redoRequested()
    signal copyRequested()
    signal pasteRequested()
    signal minimizeRequested()
    signal zoomRequested()
    signal mainWindowRequested()
    signal nodeRequested()
    signal activityRequested()
    signal sendRequested()
    signal receiveRequested()
    signal informationRequested()
    signal consoleRequested()
    signal networkTrafficRequested()
    signal peersRequested()
    signal rpcDocumentationRequested()
    signal aboutRequested()

    readonly property MenuCommand createWallet: MenuCommand {
        text: qsTr("&Add Wallet…")
        visible: root.walletMode
        enabled: root.walletManagementEnabled
        onTriggered: root.createWalletRequested()
    }
    readonly property MenuCommand closeWallet: MenuCommand {
        text: qsTr("Close Wallet…")
        visible: root.walletMode
        enabled: root.loadedWalletEnabled
        onTriggered: root.closeWalletRequested()
    }
    readonly property MenuCommand backupWallet: MenuCommand {
        text: qsTr("&Backup Wallet…")
        visible: root.walletMode
        enabled: root.loadedWalletEnabled
        onTriggered: root.backupWalletRequested()
    }
    readonly property MenuCommand openUri: MenuCommand {
        text: qsTr("Open &URI…")
        visible: root.walletMode
        enabled: root.loadedWalletEnabled
        onTriggered: root.openUriRequested()
    }
    readonly property MenuCommand signMessage: MenuCommand {
        text: qsTr("Sign &message…")
        visible: root.walletMode
        enabled: root.loadedWalletEnabled
        onTriggered: root.signMessageRequested()
    }
    readonly property MenuCommand verifyMessage: MenuCommand {
        text: qsTr("&Verify message…")
        visible: root.walletMode
        enabled: root.loadedWalletEnabled
        onTriggered: root.verifyMessageRequested()
    }
    readonly property MenuCommand loadPsbt: MenuCommand {
        text: qsTr("&Load PSBT from file…")
        visible: root.walletMode
        enabled: root.loadedWalletEnabled
        onTriggered: root.loadPsbtRequested()
    }
    readonly property MenuCommand exit: MenuCommand {
        text: qsTr("E&xit")
        shortcut: qsTr("Ctrl+Q")
        enabled: root.commandEnabled
        onTriggered: root.exitRequested()
    }

    readonly property MenuCommand settings: MenuCommand {
        text: qsTr("&Settings…")
        enabled: root.routeEnabled
        onTriggered: root.settingsRequested()
    }

    readonly property MenuCommand undo: MenuCommand {
        text: qsTr("&Undo")
        shortcut: StandardKey.Undo
        enabled: root.commandEnabled && root.canUndo
        onTriggered: root.undoRequested()
    }
    readonly property MenuCommand redo: MenuCommand {
        text: qsTr("&Redo")
        shortcut: StandardKey.Redo
        enabled: root.commandEnabled && root.canRedo
        onTriggered: root.redoRequested()
    }
    readonly property MenuCommand copy: MenuCommand {
        text: qsTr("&Copy")
        shortcut: StandardKey.Copy
        enabled: root.commandEnabled && root.canCopy
        onTriggered: root.copyRequested()
    }
    readonly property MenuCommand paste: MenuCommand {
        text: qsTr("&Paste")
        shortcut: StandardKey.Paste
        enabled: root.commandEnabled && root.canPaste
        onTriggered: root.pasteRequested()
    }
    readonly property MenuCommand minimize: MenuCommand {
        text: qsTr("&Minimize")
        shortcut: qsTr("Ctrl+M")
        enabled: root.commandEnabled
        onTriggered: root.minimizeRequested()
    }
    readonly property MenuCommand zoom: MenuCommand {
        text: qsTr("Zoom")
        visible: root.isMacOs
        enabled: root.commandEnabled
        onTriggered: root.zoomRequested()
    }
    readonly property MenuCommand mainWindow: MenuCommand {
        text: qsTr("Main Window")
        visible: root.isMacOs
        enabled: root.commandEnabled
        onTriggered: root.mainWindowRequested()
    }
    readonly property MenuCommand nodeView: MenuCommand {
        text: qsTr("&Node")
        shortcut: qsTr("Ctrl+0")
        enabled: root.routeEnabled
        onTriggered: root.nodeRequested()
    }
    readonly property MenuCommand activityView: MenuCommand {
        text: qsTr("&Activity")
        shortcut: qsTr("Ctrl+1")
        visible: root.walletMode
        enabled: root.loadedWalletEnabled
        onTriggered: root.activityRequested()
    }
    readonly property MenuCommand sendView: MenuCommand {
        text: qsTr("&Send")
        shortcut: qsTr("Ctrl+2")
        visible: root.walletMode
        enabled: root.loadedWalletEnabled
        onTriggered: root.sendRequested()
    }
    readonly property MenuCommand receiveView: MenuCommand {
        text: qsTr("&Receive")
        shortcut: qsTr("Ctrl+3")
        visible: root.walletMode
        enabled: root.loadedWalletEnabled
        onTriggered: root.receiveRequested()
    }
    readonly property MenuCommand information: MenuCommand {
        text: qsTr("Information")
        shortcut: qsTr("Ctrl+I")
        enabled: root.routeEnabled
        onTriggered: root.informationRequested()
    }
    readonly property MenuCommand consoleView: MenuCommand {
        text: qsTr("Console")
        shortcut: qsTr("Ctrl+T")
        enabled: root.routeEnabled
        onTriggered: root.consoleRequested()
    }
    readonly property MenuCommand networkTraffic: MenuCommand {
        text: qsTr("Network Traffic")
        shortcut: qsTr("Ctrl+N")
        enabled: root.routeEnabled
        onTriggered: root.networkTrafficRequested()
    }
    readonly property MenuCommand peers: MenuCommand {
        text: qsTr("Peers")
        shortcut: qsTr("Ctrl+P")
        enabled: root.routeEnabled
        onTriggered: root.peersRequested()
    }

    readonly property MenuCommand rpcDocumentation: MenuCommand {
        text: qsTr("RPC Documentation")
        enabled: root.commandEnabled
        onTriggered: root.rpcDocumentationRequested()
    }
    readonly property MenuCommand about: MenuCommand {
        text: qsTr("&About Bitcoin Core")
        enabled: root.routeEnabled
        onTriggered: root.aboutRequested()
    }
}

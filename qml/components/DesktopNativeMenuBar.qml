// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQml 2.15
import Qt.labs.platform 1.1 as Platform

Platform.MenuBar {
    id: root
    objectName: "desktopNativeMenuBar"

    property DesktopMenuActions actions
    property bool active: false

    Platform.Menu {
        objectName: "fileMenu"
        title: root.actions.fileMenuTitle
        visible: root.active
        enabled: root.active && !root.actions.shuttingDown

        Platform.MenuItem {
            objectName: "menuCreateWallet"
            text: root.actions.createWallet.text
            visible: root.actions.createWallet.visible
            enabled: root.actions.createWallet.enabled
            shortcut: root.actions.createWallet.shortcut
            onTriggered: root.actions.createWallet.trigger()
        }
        Platform.MenuItem {
            objectName: "menuCloseWallet"
            text: root.actions.closeWallet.text
            visible: root.actions.closeWallet.visible
            enabled: root.actions.closeWallet.enabled
            shortcut: root.actions.closeWallet.shortcut
            onTriggered: root.actions.closeWallet.trigger()
        }
        Platform.MenuSeparator { visible: root.actions.walletMode }
        Platform.MenuItem {
            objectName: "menuBackupWallet"
            text: root.actions.backupWallet.text
            visible: root.actions.backupWallet.visible
            enabled: root.actions.backupWallet.enabled
            shortcut: root.actions.backupWallet.shortcut
            onTriggered: root.actions.backupWallet.trigger()
        }
        Platform.MenuSeparator { visible: root.actions.walletMode }
        Platform.MenuItem {
            objectName: "menuOpenUri"
            text: root.actions.openUri.text
            visible: root.actions.openUri.visible
            enabled: root.actions.openUri.enabled
            shortcut: root.actions.openUri.shortcut
            onTriggered: root.actions.openUri.trigger()
        }
        Platform.MenuItem {
            objectName: "menuSignMessage"
            text: root.actions.signMessage.text
            visible: root.actions.signMessage.visible
            enabled: root.actions.signMessage.enabled
            shortcut: root.actions.signMessage.shortcut
            onTriggered: root.actions.signMessage.trigger()
        }
        Platform.MenuItem {
            objectName: "menuVerifyMessage"
            text: root.actions.verifyMessage.text
            visible: root.actions.verifyMessage.visible
            enabled: root.actions.verifyMessage.enabled
            shortcut: root.actions.verifyMessage.shortcut
            onTriggered: root.actions.verifyMessage.trigger()
        }
        Platform.MenuItem {
            objectName: "menuLoadPsbt"
            text: root.actions.loadPsbt.text
            visible: root.actions.loadPsbt.visible
            enabled: root.actions.loadPsbt.enabled
            shortcut: root.actions.loadPsbt.shortcut
            onTriggered: root.actions.loadPsbt.trigger()
        }
        Platform.MenuSeparator { visible: root.actions.walletMode }
        Platform.MenuItem {
            objectName: "menuSettings"
            text: root.actions.settings.text
            visible: root.actions.settings.visible
            enabled: root.actions.settings.enabled
            shortcut: root.actions.settings.shortcut
            role: Platform.MenuItem.PreferencesRole
            onTriggered: root.actions.settings.trigger()
        }
        Platform.MenuSeparator {}
        Platform.MenuItem {
            objectName: "menuExit"
            text: root.actions.exit.text
            visible: root.actions.exit.visible
            enabled: root.actions.exit.enabled
            shortcut: root.actions.exit.shortcut
            role: Platform.MenuItem.QuitRole
            onTriggered: root.actions.exit.trigger()
        }
    }

    Platform.Menu {
        objectName: "editMenu"
        title: root.actions.editMenuTitle
        visible: root.active
        enabled: root.active && !root.actions.shuttingDown

        Platform.MenuItem {
            objectName: "menuUndo"
            text: root.actions.undo.text
            enabled: root.actions.undo.enabled
            shortcut: root.actions.undo.shortcut
            onTriggered: root.actions.undo.trigger()
        }
        Platform.MenuItem {
            objectName: "menuRedo"
            text: root.actions.redo.text
            enabled: root.actions.redo.enabled
            shortcut: root.actions.redo.shortcut
            onTriggered: root.actions.redo.trigger()
        }
        Platform.MenuSeparator {}
        Platform.MenuItem {
            objectName: "menuCopy"
            text: root.actions.copy.text
            enabled: root.actions.copy.enabled
            shortcut: root.actions.copy.shortcut
            role: Platform.MenuItem.CopyRole
            onTriggered: root.actions.copy.trigger()
        }
        Platform.MenuItem {
            objectName: "menuPaste"
            text: root.actions.paste.text
            enabled: root.actions.paste.enabled
            shortcut: root.actions.paste.shortcut
            role: Platform.MenuItem.PasteRole
            onTriggered: root.actions.paste.trigger()
        }
    }

    Platform.Menu {
        objectName: "viewMenu"
        title: root.actions.viewMenuTitle
        visible: root.active
        enabled: root.active && !root.actions.shuttingDown

        Platform.MenuItem {
            objectName: "menuNode"
            text: root.actions.nodeView.text
            enabled: root.actions.nodeView.enabled
            shortcut: root.actions.nodeView.shortcut
            onTriggered: root.actions.nodeView.trigger()
        }
        Platform.MenuItem {
            objectName: "menuActivity"
            text: root.actions.activityView.text
            visible: root.actions.activityView.visible
            enabled: root.actions.activityView.enabled
            shortcut: root.actions.activityView.shortcut
            onTriggered: root.actions.activityView.trigger()
        }
        Platform.MenuItem {
            objectName: "menuSend"
            text: root.actions.sendView.text
            visible: root.actions.sendView.visible
            enabled: root.actions.sendView.enabled
            shortcut: root.actions.sendView.shortcut
            onTriggered: root.actions.sendView.trigger()
        }
        Platform.MenuItem {
            objectName: "menuReceive"
            text: root.actions.receiveView.text
            visible: root.actions.receiveView.visible
            enabled: root.actions.receiveView.enabled
            shortcut: root.actions.receiveView.shortcut
            onTriggered: root.actions.receiveView.trigger()
        }
        Platform.MenuSeparator { visible: root.actions.walletMode }
        Platform.MenuItem {
            objectName: "menuInformation"
            text: root.actions.information.text
            enabled: root.actions.information.enabled
            shortcut: root.actions.information.shortcut
            onTriggered: root.actions.information.trigger()
        }
        Platform.MenuItem {
            objectName: "menuConsole"
            text: root.actions.consoleView.text
            enabled: root.actions.consoleView.enabled
            shortcut: root.actions.consoleView.shortcut
            onTriggered: root.actions.consoleView.trigger()
        }
        Platform.MenuItem {
            objectName: "menuNetworkTraffic"
            text: root.actions.networkTraffic.text
            enabled: root.actions.networkTraffic.enabled
            shortcut: root.actions.networkTraffic.shortcut
            onTriggered: root.actions.networkTraffic.trigger()
        }
        Platform.MenuItem {
            objectName: "menuPeers"
            text: root.actions.peers.text
            enabled: root.actions.peers.enabled
            shortcut: root.actions.peers.shortcut
            onTriggered: root.actions.peers.trigger()
        }
        Platform.MenuSeparator {
            objectName: "viewTrailingSeparator"
        }
    }

    Platform.Menu {
        objectName: "windowMenu"
        title: root.actions.windowMenuTitle
        visible: root.active
        enabled: root.active && !root.actions.shuttingDown

        Platform.MenuItem {
            objectName: "menuMinimize"
            text: root.actions.minimize.text
            visible: root.actions.minimize.visible
            enabled: root.actions.minimize.enabled
            shortcut: root.actions.minimize.shortcut
            onTriggered: root.actions.minimize.trigger()
        }
        Platform.MenuItem {
            objectName: "menuZoom"
            text: root.actions.zoom.text
            visible: root.actions.zoom.visible
            enabled: root.actions.zoom.enabled
            shortcut: root.actions.zoom.shortcut
            onTriggered: root.actions.zoom.trigger()
        }
        Platform.MenuSeparator { visible: root.actions.mainWindow.visible }
        Platform.MenuItem {
            objectName: "menuMainWindow"
            text: root.actions.mainWindow.text
            visible: root.actions.mainWindow.visible
            enabled: root.actions.mainWindow.enabled
            shortcut: root.actions.mainWindow.shortcut
            onTriggered: root.actions.mainWindow.trigger()
        }
    }

    Platform.Menu {
        objectName: "helpMenu"
        title: root.actions.helpMenuTitle
        visible: root.active
        enabled: root.active && !root.actions.shuttingDown

        Platform.MenuItem {
            objectName: "menuRpcDocumentation"
            text: root.actions.rpcDocumentation.text
            enabled: root.actions.rpcDocumentation.enabled
            shortcut: root.actions.rpcDocumentation.shortcut
            onTriggered: root.actions.rpcDocumentation.trigger()
        }
        Platform.MenuItem {
            objectName: "menuAbout"
            text: root.actions.about.text
            visible: root.actions.about.visible
            enabled: root.actions.about.enabled
            shortcut: root.actions.about.shortcut
            role: Platform.MenuItem.AboutRole
            onTriggered: root.actions.about.trigger()
        }
    }
}

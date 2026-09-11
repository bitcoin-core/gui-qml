// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtTest 1.2
import "../../qml/components"

TestCase {
    id: testCase
    name: "DesktopMenuActions"

    property var actionsUnderTest: null

    Component {
        id: actionsComponent
        DesktopMenuActions {}
    }

    SignalSpy {
        id: exitSpy
        target: testCase.actionsUnderTest
        signalName: "exitRequested"
    }

    SignalSpy {
        id: undoSpy
        target: testCase.actionsUnderTest
        signalName: "undoRequested"
    }

    SignalSpy {
        id: rpcDocumentationSpy
        target: testCase.actionsUnderTest
        signalName: "rpcDocumentationRequested"
    }

    function init() {
        actionsUnderTest = actionsComponent.createObject(testCase)
        verify(actionsUnderTest !== null)
    }

    function cleanup() {
        if (actionsUnderTest) {
            actionsUnderTest.destroy()
            actionsUnderTest = null
        }
        exitSpy.clear()
        undoSpy.clear()
        rpcDocumentationSpy.clear()
    }

    function enableLoadedWallet() {
        actionsUnderTest.walletMode = true
        actionsUnderTest.walletInitialized = true
        actionsUnderTest.walletLoaded = true
    }

    function test_node_mode_omits_wallet_commands() {
        compare(actionsUnderTest.createWallet.visible, false)
        compare(actionsUnderTest.activityView.visible, false)
        compare(actionsUnderTest.sendView.visible, false)
        compare(actionsUnderTest.receiveView.visible, false)
        compare(actionsUnderTest.settings.visible, true)
        compare(actionsUnderTest.nodeView.enabled, true)
        compare(actionsUnderTest.consoleView.enabled, true)
        compare(actionsUnderTest.exit.enabled, true)
    }

    function test_wallet_commands_follow_lifecycle_state() {
        actionsUnderTest.walletMode = true
        compare(actionsUnderTest.createWallet.enabled, false)
        compare(actionsUnderTest.closeWallet.enabled, false)

        actionsUnderTest.walletInitialized = true
        compare(actionsUnderTest.createWallet.enabled, true)
        compare(actionsUnderTest.closeWallet.enabled, false)
        compare(actionsUnderTest.backupWallet.enabled, false)
        compare(actionsUnderTest.openUri.enabled, false)
        compare(actionsUnderTest.signMessage.enabled, false)
        compare(actionsUnderTest.verifyMessage.enabled, false)
        compare(actionsUnderTest.loadPsbt.enabled, false)
        compare(actionsUnderTest.activityView.enabled, false)
        compare(actionsUnderTest.sendView.enabled, false)
        compare(actionsUnderTest.receiveView.enabled, false)

        actionsUnderTest.walletLoaded = true
        compare(actionsUnderTest.closeWallet.enabled, true)
        compare(actionsUnderTest.backupWallet.enabled, true)
        compare(actionsUnderTest.openUri.enabled, true)
        compare(actionsUnderTest.signMessage.enabled, true)
        compare(actionsUnderTest.verifyMessage.enabled, true)
        compare(actionsUnderTest.loadPsbt.enabled, true)
        compare(actionsUnderTest.activityView.enabled, true)
        compare(actionsUnderTest.sendView.enabled, true)
        compare(actionsUnderTest.receiveView.enabled, true)

        actionsUnderTest.walletBusy = true
        compare(actionsUnderTest.createWallet.enabled, false)
        compare(actionsUnderTest.closeWallet.enabled, false)
        compare(actionsUnderTest.backupWallet.enabled, false)
        compare(actionsUnderTest.openUri.enabled, false)
        compare(actionsUnderTest.signMessage.enabled, false)
        compare(actionsUnderTest.verifyMessage.enabled, false)
        compare(actionsUnderTest.loadPsbt.enabled, false)
        compare(actionsUnderTest.activityView.enabled, false)
        compare(actionsUnderTest.sendView.enabled, false)
        compare(actionsUnderTest.receiveView.enabled, false)
    }

    function test_edit_commands_follow_target_state_and_emit_requests() {
        compare(actionsUnderTest.undo.enabled, false)
        compare(actionsUnderTest.redo.enabled, false)
        compare(actionsUnderTest.copy.enabled, false)
        compare(actionsUnderTest.paste.enabled, false)

        actionsUnderTest.canUndo = true
        actionsUnderTest.canRedo = true
        actionsUnderTest.canCopy = true
        actionsUnderTest.canPaste = true
        compare(actionsUnderTest.undo.enabled, true)
        compare(actionsUnderTest.redo.enabled, true)
        compare(actionsUnderTest.copy.enabled, true)
        compare(actionsUnderTest.paste.enabled, true)

        actionsUnderTest.undo.trigger()
        compare(undoSpy.count, 1)
    }

    function test_shutdown_disables_commands_and_blocks_trigger() {
        actionsUnderTest.shuttingDown = true
        compare(actionsUnderTest.exit.enabled, false)
        compare(actionsUnderTest.settings.enabled, false)
        actionsUnderTest.canUndo = true
        compare(actionsUnderTest.undo.enabled, false)
        actionsUnderTest.exit.trigger()
        compare(exitSpy.count, 0)
    }

    function test_enabled_command_emits_request() {
        actionsUnderTest.exit.trigger()
        compare(exitSpy.count, 1)

        actionsUnderTest.rpcDocumentation.trigger()
        compare(rpcDocumentationSpy.count, 1)
    }
}

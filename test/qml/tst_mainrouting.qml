// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtTest 1.2
import org.bitcoincore.qt 1.0
import "../../qml/pages"
import "../../qml/pages/wallet"

TestCase {
    id: testCase
    name: "MainRouting"
    when: windowShown
    width: 900
    height: 600

    property bool walletAvailable: true
    property bool preInitOnboardingRan: false
    property var windowUnderTest: null

    SignalSpy {
        id: shutdownSpy
        target: nodeModel
        signalName: "requestedShutdown"
    }

    Component {
        id: mainWindowComponent
        MainWindow {
            walletAvailableForUi: testCase.walletAvailable
            preInitOnboardingRanForUi: testCase.preInitOnboardingRan
            nativeMenuAvailableForUi: false
        }
    }

    function cleanup() {
        if (windowUnderTest) {
            windowUnderTest.close()
            windowUnderTest.destroy()
            windowUnderTest = null
        }
        nodeModel.fatalException = false
        nodeModel.setStartupErrorForTest("")
        desktopWindowBehaviorModel.minimizeOnClose = false
        shutdownSpy.clear()
    }

    function createMain(wallet_enabled, preinit_onboarding_ran, no_wallets_found, wallet_dir_loaded, initialized) {
        cleanup()
        walletAvailable = wallet_enabled
        preInitOnboardingRan = preinit_onboarding_ran || false
        walletController.reset()
        walletListModel.reset()
        walletController.setInitialized(initialized === undefined ? true : initialized)
        walletController.setWalletLoaded(!no_wallets_found)
        walletController.setNoWalletsFound(no_wallets_found || false)
        walletListModel.setWalletDirLoaded(wallet_dir_loaded === undefined ? true : wallet_dir_loaded)
        windowUnderTest = mainWindowComponent.createObject(null)
        verify(windowUnderTest !== null)
        wait(0)
        return windowUnderTest
    }

    function test_wallet_available_routes_to_desktop_wallets() {
        const window = createMain(true)
        verify(findChild(window, "mainPageStack") !== null)
        verify(findChild(window, "desktopWalletsPage") !== null)
        verify(findChild(window, "walletBadge") !== null)
        verify(findChild(window, "nodeRunner") === null)
        const menuActions = findChild(window, "desktopMenuActions")
        verify(menuActions !== null)
        compare(menuActions.createWallet.visible, true)
        compare(menuActions.createWallet.enabled, true)
    }

    function test_fatal_exception_during_shutdown_keeps_dialog_usable() {
        const window = createMain(false)
        nodeModel.requestShutdown()
        window.contentItem.enabled = false
        window.hide()

        nodeModel.fatalException = true
        nodeModel.setStartupErrorForTest("Shutdown failed")
        const popup = findChild(window, "nodeFatalErrorPopup")
        tryCompare(popup, "opened", true)
        verify(window.visible)
        verify(window.contentItem.enabled)
        compare(findChild(window, "mainPageStack").enabled, false)
        const actions = findChild(window, "desktopMenuActions")
        compare(actions.settings.enabled, false)
        compare(actions.exit.enabled, true)
        const button = findChild(popup, "nodeFatalShutdownButton")
        verify(button.enabled)
        verify(waitForRendering(button))
        shutdownSpy.clear()
        mouseClick(button, button.width / 2, button.height / 2)
        compare(shutdownSpy.count, 1)
    }

    function test_closing_fatal_exception_requests_exit_even_with_minimize_on_close() {
        const window = createMain(false)
        desktopWindowBehaviorModel.minimizeOnClose = true
        nodeModel.fatalException = true
        nodeModel.setStartupErrorForTest("Initialization failed")
        tryCompare(findChild(window, "nodeFatalErrorPopup"), "opened", true)
        shutdownSpy.clear()
        window.close()
        compare(shutdownSpy.count, 1)
    }

    function test_wallet_unavailable_routes_to_node_runner() {
        const window = createMain(false)
        verify(findChild(window, "mainPageStack") !== null)
        verify(findChild(window, "nodeRunner") !== null)
        verify(findChild(window, "walletBadge") === null)
        const menuActions = findChild(window, "desktopMenuActions")
        verify(menuActions !== null)
        compare(menuActions.createWallet.visible, false)
        compare(menuActions.settings.enabled, true)
    }

    function test_add_wallet_menu_command_opens_wallet_wizard() {
        const window = createMain(true)
        const menuActions = findChild(window, "desktopMenuActions")

        menuActions.createWallet.trigger()
        const wizard = findChild(window, "createWalletWizard")
        verify(wizard !== null)
        compare(findChild(window, "mainPageStack").depth, 1)
        compare(findChild(window, "walletCreationModal").visible, true)
        const typePage = findChild(wizard, "walletCreationTypePage")
        verify(typePage !== null)
        compare(typePage.title, "")
        compare(typePage.heading, "Choose a wallet type")
        compare(typePage.subheading, "You can create a new wallet or import from a wallet file.")
        compare(findChild(typePage, "walletTypeImport").description,
            "Use an existing wallet backup file.")
    }

    function test_add_wallet_completion_returns_to_activity() {
        const window = createMain(true)
        findChild(window, "desktopMenuActions").createWallet.trigger()
        const wizard = findChild(window, "createWalletWizard")
        verify(wizard !== null)
        findChild(wizard, "walletTypeViewOnly").clicked()
        tryVerify(function() { return findChild(wizard, "createWalletFormPage") !== null })
        const form = findChild(wizard, "createWalletFormPage")
        findChild(form, "createWalletNameInput").text = "Watch wallet"
        findChild(form, "watchOnlyXpubInput").text =
            "xpub661MyMwAqRbcFtXgS5sYJABqqG9YLmC4Q1Rdap9gSE8NqtwybGhePY2gZ29ESFjqJoCu1Rupje8YtGqsefD265TMg7usUDFdp6W1EGMcet8"
        findChild(form, "createWalletFormCreateButton").clicked()
        tryVerify(function() { return findChild(wizard, "walletCreationReadyPage") !== null })
        findChild(wizard, "createWalletReadyDoneButton").clicked()

        tryCompare(findChild(window, "walletCreationModal"), "visible", false)
        compare(findChild(window, "mainPageStack").depth, 1)
        compare(findChild(window, "activityTabButton").checked, true)
    }

    function test_onboarding_creation_returns_to_activity() {
        const window = createMain(true, true, true, true, true)
        tryCompare(findChild(window, "walletCreationModal"), "visible", true)
        const wizard = findChild(window, "createWalletWizard")
        verify(wizard !== null)
        const typePage = findChild(wizard, "walletCreationTypePage")
        verify(typePage !== null)
        compare(typePage.title, "")
        compare(typePage.heading, "Add a wallet to your node")
        compare(typePage.subheading,
            "Add a wallet to start using Bitcoin Core. You can create a new wallet now or import from wallet file.")
        compare(findChild(window, "blockClockTabButton").checked, true)
        findChild(wizard, "walletTypeViewOnly").clicked()
        tryVerify(function() { return findChild(wizard, "createWalletFormPage") !== null })
        const form = findChild(wizard, "createWalletFormPage")
        findChild(form, "createWalletNameInput").text = "Onboarding watch wallet"
        findChild(form, "watchOnlyXpubInput").text =
            "xpub661MyMwAqRbcFtXgS5sYJABqqG9YLmC4Q1Rdap9gSE8NqtwybGhePY2gZ29ESFjqJoCu1Rupje8YtGqsefD265TMg7usUDFdp6W1EGMcet8"
        findChild(form, "createWalletFormCreateButton").clicked()
        tryVerify(function() { return findChild(wizard, "walletCreationReadyPage") !== null })
        walletController.setWalletLoaded(true)
        findChild(wizard, "createWalletReadyDoneButton").clicked()

        tryCompare(findChild(window, "walletCreationModal"), "visible", false)
        tryCompare(findChild(window, "walletCreationModal"), "onboardingEntry", false)
        compare(findChild(window, "mainPageStack").depth, 1)
        compare(findChild(window, "activityTabButton").checked, true)
    }

    function test_view_menu_commands_select_wallet_tabs() {
        const window = createMain(true)
        const menuActions = findChild(window, "desktopMenuActions")
        const activityTab = findChild(window, "activityTabButton")
        const sendTab = findChild(window, "sendTabButton")
        const receiveTab = findChild(window, "receiveTabButton")
        const nodeTab = findChild(window, "blockClockTabButton")

        verify(menuActions !== null)
        verify(activityTab !== null)
        verify(sendTab !== null)
        verify(receiveTab !== null)
        verify(nodeTab !== null)

        menuActions.nodeView.trigger()
        compare(nodeTab.checked, true)
        menuActions.sendView.trigger()
        compare(sendTab.checked, true)
        menuActions.receiveView.trigger()
        compare(receiveTab.checked, true)
        menuActions.activityView.trigger()
        compare(activityTab.checked, true)
    }

    function test_edit_menu_toggles_display_unit() {
        const window = createMain(true)
        const menuActions = findChild(window, "desktopMenuActions")
        verify(menuActions !== null)

        optionsModel.displayUnit = BitcoinAmount.BTC
        menuActions.toggleDisplayUnit.trigger()
        compare(optionsModel.displayUnit, BitcoinAmount.SAT)
        menuActions.toggleDisplayUnit.trigger()
        compare(optionsModel.displayUnit, BitcoinAmount.BTC)
    }

    function test_undo_does_not_modify_hidden_send_field() {
        const window = createMain(true)
        const menuActions = findChild(window, "desktopMenuActions")
        const activityTab = findChild(window, "activityTabButton")
        window.show()
        menuActions.sendView.trigger()
        testRecipientsModel.clearToFront()
        tryVerify(function() { return findChild(window.contentItem, "sendNoteInput") !== null })
        const noteInput = findChild(window.contentItem, "sendNoteInput")

        verify(menuActions !== null)
        verify(activityTab !== null)
        verify(noteInput !== null)

        menuActions.sendView.trigger()
        noteInput.forceActiveFocus()
        tryCompare(noteInput, "activeFocus", true)
        noteInput.cursorPosition = noteInput.text.length
        keyClick("x")
        const editedText = noteInput.text
        tryCompare(menuActions.undo, "enabled", true)

        menuActions.activityView.trigger()
        compare(activityTab.checked, true)
        tryCompare(menuActions.undo, "enabled", false)

        menuActions.undo.trigger()
        compare(noteInput.text, editedText)
    }

    function test_preinit_onboarding_shows_node_while_wallet_scan_pending() {
        const window = createMain(true, true, false, false, false)
        verify(findChild(window, "desktopWalletsPage") !== null)
        compare(findChild(window, "blockClockTabButton").checked, true)
        verify(findChild(window, "createWalletWizard") === null)
        verify(findChild(window, "nodeRunner") === null)
    }

    function test_preinit_onboarding_no_wallets_opens_modal_over_node_after_scan() {
        const window = createMain(true, true, true, false, false)
        verify(findChild(window, "desktopWalletsPage") !== null)
        verify(findChild(window, "createWalletWizard") === null)
        walletController.setInitialized(true)
        walletListModel.setWalletDirLoaded(true)
        tryCompare(findChild(window, "walletCreationModal"), "visible", true)
        verify(findChild(window, "desktopWalletsPage") !== null)
        verify(findChild(window, "walletBadge") !== null)
        compare(findChild(window, "mainPageStack").depth, 1)
        compare(findChild(window, "blockClockTabButton").checked, true)
        verify(findChild(window, "walletCreationTypePage") !== null)
        verify(findChild(window, "nodeRunner") === null)
    }

    function test_preinit_onboarding_existing_wallets_opens_wallet_shell_after_scan() {
        const window = createMain(true, true, false, false, false)
        verify(findChild(window, "desktopWalletsPage") !== null)
        verify(findChild(window, "createWalletWizard") === null)
        walletController.setInitialized(true)
        walletListModel.setWalletDirLoaded(true)
        tryVerify(function() {
            return findChild(window, "desktopWalletsPage") !== null
        })
        verify(findChild(window, "desktopWalletsPage") !== null)
        verify(findChild(window, "createWalletWizard") === null)
        verify(findChild(window, "nodeRunner") === null)
    }

    function test_normal_restart_no_wallets_does_not_open_create_wallet_wizard() {
        const window = createMain(true, false, true)
        verify(findChild(window, "desktopWalletsPage") !== null)
        verify(findChild(window, "walletBadge") !== null)
        verify(findChild(window, "createWalletWizard") === null)
    }
}

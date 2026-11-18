// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Window 2.15
import QtTest 1.2
import org.bitcoincore.qt 1.0
import "../../qml/pages"
import "../../qml/pages/wallet"
import "../../qml/controls" as Controls

TestCase {
    function init() {
        testSetup.failOnWarnings()
    }

    id: testCase
    name: "MainRouting"
    when: windowShown
    width: 900
    height: 600

    property bool walletAvailable: true
    property bool preInitOnboardingRan: false
    property var windowUnderTest: null
    property var mainGapSpies: []
    property var geometryPreferences: null

    Component { id: peerRefreshSpy; SignalSpy { signalName: "autoRefreshActiveChanged" } }
    Component { id: walletCreatedSpy; SignalSpy { signalName: "walletCreateSucceeded" } }
    Component { id: trayVisibilitySpy; SignalSpy { signalName: "windowVisibleChanged" } }
    Component { id: windowXSpy; SignalSpy { signalName: "xChanged" } }
    Component { id: preferencesComponent; AppPreferences {} }

    function gapSpy(component, target) {
        const spy = createTemporaryObject(component, testCase, {target: target})
        verify(spy !== null)
        mainGapSpies.push(spy)
        return spy
    }

    function clickVisible(item) {
        verify(item !== null)
        verify(item.visible && item.enabled)
        compare(item.Window.window, windowUnderTest)
        verify(windowUnderTest.visible)
        // Some software backends capture an unchanged frame without another
        // frameSwapped signal; others expose only the frame notification.
        const image = grabImage(windowUnderTest.contentItem)
        verify(image !== null)
        if (image.width === 0 || image.height === 0) {
            windowUnderTest.update()
            verify(waitForRendering(item))
        }
        const point = item.mapToItem(windowUnderTest.contentItem, item.width / 2, item.height / 2)
        const bounds = JSON.stringify({name: item.objectName, x: point.x, y: point.y,
            contentWidth: windowUnderTest.contentItem.width, contentHeight: windowUnderTest.contentItem.height,
            contentX: windowUnderTest.contentItem.x, contentY: windowUnderTest.contentItem.y,
            itemWidth: item.width, itemHeight: item.height})
        verify(point.x >= 0 && point.x < windowUnderTest.contentItem.width, bounds)
        verify(point.y >= 0 && point.y < windowUnderTest.contentItem.height, bounds)
        mouseClick(item, item.width / 2, item.height / 2)
    }

    function selectedSend(window) {
        findChild(window, "desktopMenuActions").sendView.trigger()
        const desktop = findChild(window, "desktopWalletsPage")
        verify(desktop !== null && desktop.activeView !== null)
        compare(desktop.activeView.wallet, walletController.selectedWallet)
        compare(findChild(window, "sendTabButton").checked, true)
        return desktop.activeView.sendPage
    }

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
        nodeModel.answerRuntimeDialog(DialogButtonBox.Ok)
        // The shared targets outlive temporary QtTest creation contexts.
        for (let i = 0; i < mainGapSpies.length; ++i) mainGapSpies[i].target = null
        mainGapSpies = []
        if (windowUnderTest) {
            // The shared mock outlives this window. Retire its delegates while
            // their creation context is alive, including deferred descendants.
            const desktop = findChild(windowUnderTest, "desktopWalletsPage")
            const receipts = []
            if (desktop) {
                for (let i = 0; i < desktop.walletViews.count; ++i) {
                    const receipt = testSetup.watchDestruction(desktop.walletViews.itemAt(i))
                    verify(receipt.count > 0)
                    receipts.push(receipt)
                }
            }
            walletController.setOpenWallets([])
            for (let i = 0; i < receipts.length; ++i) {
                tryCompare(receipts[i], "pending", 0)
                testSetup.releaseDestructionWatch(receipts[i])
            }
            // Some tests create another window before returning to QtTest.
            // Observe deletion before resetting the shared model for that one.
            const windowReceipt = testSetup.watchDestruction(windowUnderTest)
            windowUnderTest.close()
            windowUnderTest.destroy()
            windowUnderTest = null
            tryCompare(windowReceipt, "pending", 0)
            testSetup.releaseDestructionWatch(windowReceipt)
        }
        if (geometryPreferences) {
            const receipt = testSetup.watchDestruction(geometryPreferences)
            geometryPreferences.destroy()
            geometryPreferences = null
            tryCompare(receipt, "pending", 0)
            testSetup.releaseDestructionWatch(receipt)
        }
        verify(testSetup.endControlledPreferences())
        desktopWindowBehaviorModel.showTrayIcon = false
        desktopWindowBehaviorModel.minimizeToTray = false
        testWalletModel.holdPsbtPublicationForTest = false
        nodeModel.fatalException = false
        nodeModel.setStartupErrorForTest("")
        desktopWindowBehaviorModel.minimizeOnClose = false
        shutdownSpy.clear()
        // Collect at a controlled point: the suite creates and destroys a full
        // window per case, and an allocation-triggered full collection inside
        // a later case has crashed in QtQml's GC state machine.
        gc()
    }

    function createMain(wallet_enabled, preinit_onboarding_ran, no_wallets_found, wallet_dir_loaded, initialized, beforeConstruct) {
        cleanup()
        walletAvailable = wallet_enabled
        preInitOnboardingRan = preinit_onboarding_ran || false
        walletController.reset()
        walletListModel.reset()
        walletController.setInitialized(initialized === undefined ? true : initialized)
        walletController.setWalletLoaded(!no_wallets_found)
        walletController.setNoWalletsFound(no_wallets_found || false)
        walletController.setOpenWallets(no_wallets_found ? [] : [testWalletModel])
        walletController.setSelectedWalletObject(testWalletModel)
        walletListModel.setWalletDirLoaded(wallet_dir_loaded === undefined ? true : wallet_dir_loaded)
        if (beforeConstruct) beforeConstruct()
        windowUnderTest = createTemporaryObject(mainWindowComponent, testCase)
        verify(windowUnderTest !== null)
        wait(0)
        return windowUnderTest
    }

    function test_owned_runtime_prompt_reveals_the_actual_main_window_overlay() {
        const window = createMain(false)
        window.hide()
        compare(window.visible, false)
        nodeModel.setRuntimeDialogForTest("Original", "Actual MainWindow question", DialogButtonBox.Ok, true)
        const original = nodeModel.runtimePrompt
        verify(original !== null)
        tryCompare(window, "visible", true)
        const popup = findChild(window.contentItem, "nodeRuntimeDialog")
        verify(popup !== null)
        tryCompare(popup, "opened", true)
        compare(popup.prompt, original)
        compare(popup.parent, window.Overlay.overlay)
        // Alert actions are delegates in the popup's content tree.
        tryVerify(function() { return findChild(popup.contentItem, "nodeRuntimeDialogButtonOk") !== null })
        const button = findChild(popup.contentItem, "nodeRuntimeDialogButtonOk")
        clickVisible(button)
        tryCompare(nodeModel, "runtimeDialogVisible", false)
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

    function test_fatal_exception_during_shutdown_shows_dialog() {
        const window = createMain(false)
        nodeModel.requestShutdown()
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
        const button = findChild(popup.contentItem, "nodeFatalShutdownButton")
        verify(button.enabled)
        // Qt 6.2 waits for a future frameSwapped without requesting a frame.
        // Request one even when the popup already finished its last animation.
        window.update()
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
        compare(window.desktopShell, null)
        compare(window.walletCreationHost, null)
        verify(findChild(window, "mainPageStack") !== null)
        verify(findChild(window, "nodeRunner") !== null)
        verify(findChild(window, "walletBadge") === null)
        const menuActions = findChild(window, "desktopMenuActions")
        verify(menuActions !== null)
        compare(menuActions.createWallet.visible, false)
        compare(menuActions.settings.enabled, true)
    }

    function test_node_status_actions_open_the_window_popups() {
        const window = createMain(false)
        const runner = findChild(window, "nodeRunner")
        verify(runner !== null)
        const popup = findChild(window, "nodeInformationPopup")
        verify(popup !== null)
        findChild(runner, "nodeInformationButton").clicked()
        tryCompare(popup, "opened", true)
        compare(popup.parent, window.Overlay.overlay)
        popup.close()
        tryCompare(popup, "opened", false)

        nodeModel.setWarningsForTest(["Clock skew warning"])
        findChild(runner, "nodeWarningsButton").clicked()
        tryCompare(findChild(window, "nodeWarningsPopup"), "opened", true)
        nodeModel.setWarningsForTest([])
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

    function test_modal_creation_keeps_file_navigation_on_the_original_page() {
        const window = createMain(true)
        const actions = findChild(window, "desktopMenuActions")
        const send = selectedSend(window)
        const originalView = findChild(window, "desktopWalletsPage").activeView
        const originalWallet = walletController.selectedWallet
        const note = findChild(send, "sendNoteInput")
        note.text = "Original Send context"
        actions.createWallet.trigger()
        const modal = findChild(window, "walletCreationModal")
        tryCompare(modal, "opened", true)
        findChild(modal, "walletTypeRegular").clicked()
        tryVerify(function() { return findChild(modal, "createWalletFormPage") !== null })
        const form = findChild(modal, "createWalletFormPage")
        const name = findChild(form, "createWalletNameInput")
        testSetup.replaceText(name, "Unsubmitted modal draft")
        const creations = walletController.creationCalls
        const closes = walletController.closeWalletCalls

        // This is the same command a native File-menu item delivers. The
        // popup blocks pointer input, but a native menu is outside its scene.
        actions.settings.trigger()
        compare(findChild(window, "sendTabButton").checked, true)
        compare(findChild(window, "desktopWalletSettingsTabButton").checked, false)
        actions.nodeView.trigger()
        actions.closeWallet.trigger()
        compare(findChild(window, "sendTabButton").checked, true)
        compare(findChild(window, "desktopWalletsPage").activeView, originalView)
        compare(walletController.selectedWallet, originalWallet)
        compare(walletController.creationCalls, creations)
        compare(walletController.closeWalletCalls, closes)
        compare(findChild(modal, "createWalletFormPage"), form)
        compare(name.text, "Unsubmitted modal draft")
        compare(note.text, "Original Send context")
        compare(modal.visible, true)
        compare(actions.settings.enabled, false)
        compare(actions.createWallet.enabled, false)
        compare(actions.exit.enabled, true)
    }

    function test_modal_creation_rejects_a_delayed_direct_route_then_restores_admission() {
        const window = createMain(true)
        const actions = findChild(window, "desktopMenuActions")
        selectedSend(window)
        actions.createWallet.trigger()
        const modal = findChild(window, "walletCreationModal")
        tryCompare(modal, "opened", true)
        window.openSettings("display")
        compare(findChild(window, "sendTabButton").checked, true)
        compare(findChild(window, "desktopWalletSettingsTabButton").checked, false)
        const close = findChild(modal, "onboardingCloseButton")
        verify(close !== null)
        clickVisible(close)
        tryCompare(modal, "visible", false)
        tryCompare(actions.settings, "enabled", true)
        actions.settings.trigger()
        compare(findChild(window, "desktopWalletSettingsTabButton").checked, true)
    }

    function test_owned_runtime_question_blocks_navigation_without_blocking_its_answer() {
        const window = createMain(true)
        const actions = findChild(window, "desktopMenuActions")
        selectedSend(window)
        try {
        nodeModel.setRuntimeDialogForTest("Original runtime question", "Continue?", DialogButtonBox.Ok, true)
        tryCompare(findChild(window, "nodeRuntimeDialog"), "opened", true)
        actions.settings.trigger()
        compare(findChild(window, "sendTabButton").checked, true)
        compare(actions.settings.enabled, false)
        const runtimeDialog = findChild(window, "nodeRuntimeDialog")
        tryVerify(function() { return findChild(runtimeDialog.contentItem, "nodeRuntimeDialogButtonOk") !== null })
        clickVisible(findChild(runtimeDialog.contentItem, "nodeRuntimeDialogButtonOk"))
        tryCompare(nodeModel, "runtimeDialogVisible", false)
        tryCompare(actions.settings, "enabled", true)
        actions.settings.trigger()
        compare(findChild(window, "desktopWalletSettingsTabButton").checked, true)
        } finally {
            if (nodeModel.runtimeDialogVisible) nodeModel.answerRuntimeDialog(DialogButtonBox.Cancel)
        }
    }

    function test_settings_close_question_keeps_its_original_route_data() {
        return [{tag: "wallet-shell", wallet: true}, {tag: "node-shell", wallet: false}]
    }
    function test_settings_close_question_keeps_its_original_route(data) {
        optionsModel.resetSettingsEditorsForTest()
        const window = createMain(data.wallet)
        const actions = findChild(window, "desktopMenuActions")
        actions.settings.trigger()
        tryVerify(function() { return findChild(window, "settingsView") !== null })
        const settings = findChild(window, "settingsView")
        settings.selectSection("connection")
        tryCompare(settings.settingsEditor, "fresh", true)
        settings.settingsEditor.entry("proxy").editText("127.0.0.1:9250")
        tryCompare(settings.settingsEditor, "dirty", true)
        settings.requestSettingsNavigation("storage", false)
        // The public request already owns the disposition before its Domain
        // answer is projected. A native menu must not use that interval.
        actions.nodeView.trigger()
        compare(settings.visible, true)
        tryCompare(settings.settingsEditor, "closeQuestion", true)
        const original = settings.settingsEditor
        const consent = original.closeToken
        actions.nodeView.trigger()
        window.openSettings("about")
        compare(settings.visible, true)
        compare(settings.selectedSectionId, "connection")
        compare(settings.pendingSettingsSection, "storage")
        compare(settings.settingsEditor, original)
        compare(original.closeToken, consent)
        compare(original.entry("proxy").text, "127.0.0.1:9250")
        compare(optionsModel.settingsWritesForTest(), 0)
        compare(actions.nodeView.enabled, false)
        original.answerClose(consent, SettingsEditorModel.KeepEditing)
        tryCompare(original, "closeQuestion", false)
        tryCompare(actions.nodeView, "enabled", true)
        actions.nodeView.trigger()
        if (data.wallet) compare(findChild(window, "blockClockTabButton").checked, true)
        else tryVerify(function() { return findChild(window, "nodeRunner").visible })
        compare(optionsModel.settingsWritesForTest(), 0)
    }

    function test_shutdown_invalidates_the_deferred_onboarding_modal() {
        const window = createMain(true, true, true, false, false)
        walletController.setInitialized(true)
        walletListModel.setWalletDirLoaded(true)
        // Catalog publication requests the zero-delay presentation; shutdown
        // is delivered before that presentation turn, with no timer sleep.
        nodeModel.requestShutdown()
        wait(0)
        compare(window.shutdownInProgress, true)
        compare(findChild(window, "walletCreationModal").visible, false)
        compare(findChild(window, "desktopMenuActions").settings.enabled, false)
        compare(findChild(window, "createWalletWizard"), null)
    }

    function test_onboarding_wallet_presentation_waits_for_the_original_runtime_prompt() {
        const window = createMain(true, true, true, false, false)
        nodeModel.setRuntimeDialogForTest("Original runtime prompt", "Continue?", DialogButtonBox.Ok, true)
        tryCompare(findChild(window, "nodeRuntimeDialog"), "opened", true)
        walletController.setInitialized(true)
        walletListModel.setWalletDirLoaded(true)
        tryCompare(window, "postOnboardingWalletRouteResolved", true)
        wait(0)
        compare(findChild(window, "walletCreationModal").visible, false)
        compare(walletController.creationCalls, 0)
        nodeModel.answerRuntimeDialog(DialogButtonBox.Ok)
        tryCompare(nodeModel, "runtimeDialogVisible", false)
        tryCompare(findChild(window, "walletCreationModal"), "opened", true)
        compare(findChild(window, "blockClockTabButton").checked, true)
        compare(walletController.creationCalls, 0)
    }

    function test_a_new_catalog_fact_cancels_the_original_deferred_wallet_presentation() {
        const window = createMain(true, true, true, false, false)
        nodeModel.setRuntimeDialogForTest("Original runtime prompt", "Continue?", DialogButtonBox.Ok, true)
        tryCompare(findChild(window, "nodeRuntimeDialog"), "opened", true)
        walletController.setInitialized(true)
        walletListModel.setWalletDirLoaded(true)
        tryCompare(window, "postOnboardingWalletRouteResolved", true)
        walletController.setNoWalletsFound(false)
        nodeModel.answerRuntimeDialog(DialogButtonBox.Ok)
        tryCompare(nodeModel, "runtimeDialogVisible", false)
        wait(0)
        compare(findChild(window, "walletCreationModal").visible, false)
        // Returning to an empty catalog is a successor observation, not a
        // replay of the completed onboarding presentation request.
        walletController.setNoWalletsFound(true)
        wait(0)
        compare(findChild(window, "walletCreationModal").visible, false)
        compare(walletController.creationCalls, 0)
    }

    function test_shell_round_trip_retains_the_original_dirty_settings_context_data() {
        return [{tag: "wallet-shell", wallet: true}, {tag: "node-shell", wallet: false}]
    }
    function test_shell_round_trip_retains_the_original_dirty_settings_context(data) {
        optionsModel.resetSettingsEditorsForTest()
        const window = createMain(data.wallet)
        const actions = findChild(window, "desktopMenuActions")
        actions.settings.trigger()
        tryVerify(function() { return findChild(window, "settingsView") !== null })
        const original = findChild(window, "settingsView")
        original.selectSection("connection")
        tryCompare(original.settingsEditor, "fresh", true)
        const editor = original.settingsEditor
        editor.entry("proxy").editText("127.0.0.1:9251")
        tryCompare(editor, "dirty", true)
        actions.nodeView.trigger()
        if (data.wallet) compare(findChild(window, "blockClockTabButton").checked, true)
        else tryVerify(function() { return findChild(window, "nodeRunner").visible })
        actions.settings.trigger()
        tryVerify(function() {
            const page = findChild(window, "settingsView")
            return page !== null && page.visible
        })
        const returned = findChild(window, "settingsView")
        compare(returned, original)
        compare(returned.settingsEditor, editor)
        compare(editor.entry("proxy").text, "127.0.0.1:9251")
        compare(editor.dirty, true)
        compare(optionsModel.settingsWritesForTest(), 0)
    }

    function test_add_wallet_completion_returns_to_activity() {
        const window = createMain(true)
        findChild(window, "desktopMenuActions").createWallet.trigger()
        const wizard = findChild(window, "createWalletWizard")
        verify(wizard !== null)
        findChild(wizard, "walletTypeViewOnly").clicked()
        tryVerify(function() { return findChild(wizard, "createWalletFormPage") !== null })
        const form = findChild(wizard, "createWalletFormPage")
        testSetup.replaceText(findChild(form, "createWalletNameInput"), "Watch wallet")
        testSetup.replaceText(findChild(form, "watchOnlyXpubInput"), "xpub661MyMwAqRbcFtXgS5sYJABqqG9YLmC4Q1Rdap9gSE8NqtwybGhePY2gZ29ESFjqJoCu1Rupje8YtGqsefD265TMg7usUDFdp6W1EGMcet8")
        const createButton = findChild(form, "createWalletFormCreateButton")
        tryCompare(createButton, "enabled", true)
        createButton.clicked()
        tryVerify(function() { return findChild(wizard, "walletCreationReadyPage") !== null })
        const doneButton = findChild(wizard, "createWalletReadyDoneButton")
        tryCompare(doneButton, "enabled", true)
        doneButton.clicked()

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
        testSetup.replaceText(findChild(form, "createWalletNameInput"), "Onboarding watch wallet")
        testSetup.replaceText(findChild(form, "watchOnlyXpubInput"), "xpub661MyMwAqRbcFtXgS5sYJABqqG9YLmC4Q1Rdap9gSE8NqtwybGhePY2gZ29ESFjqJoCu1Rupje8YtGqsefD265TMg7usUDFdp6W1EGMcet8")
        const createButton = findChild(form, "createWalletFormCreateButton")
        tryCompare(createButton, "enabled", true)
        createButton.clicked()
        tryVerify(function() { return findChild(wizard, "walletCreationReadyPage") !== null })
        walletController.setOpenWallets([testWalletModel])
        walletController.setWalletLoaded(true)
        const doneButton = findChild(wizard, "createWalletReadyDoneButton")
        tryCompare(doneButton, "enabled", true)
        doneButton.clicked()

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
        window.requestActivate()
        tryCompare(window, "active", true)
        noteInput.forceActiveFocus()
        tryCompare(noteInput, "activeFocus", true)
        noteInput.cursorPosition = noteInput.text.length
        const originalText = noteInput.text
        keyClick(Qt.Key_X)
        compare(noteInput.text, originalText + "x")
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

    function test_node_settings_tracks_selected_opening_data() {
        return [{tag: "selected-wallet", loaded: true}, {tag: "no-wallet", loaded: false}]
    }
    function test_node_settings_tracks_selected_opening(data) {
        const window = createMain(false)
        walletController.setOpenWallets([testWalletModel, testSecondWalletModel])
        walletController.setWalletLoaded(data.loaded)
        walletController.setSelectedWalletObject(data.loaded ? testWalletModel : null)
        const stack = findChild(window, "mainPageStack").currentItem
        const originalNode = stack.currentItem
        findChild(window, "desktopMenuActions").settings.trigger()
        compare(stack.depth, 2)
        const settings = stack.currentItem
        compare(settings.objectName, "settingsView")
        compare(settings.selectedSectionId, "display")
        compare(settings.wallet, data.loaded ? testWalletModel : null)
        compare(settings.activeWalletName, data.loaded ? testWalletModel.name : "")
        if (data.loaded) {
            verify(walletController.selectWallet(testSecondWalletModel))
            compare(stack.currentItem, settings)
            compare(settings.wallet, testSecondWalletModel)
            compare(settings.activeWalletName, testSecondWalletModel.name)
        }
        clickVisible(findChild(settings, "settingsDoneButton"))
        tryCompare(stack, "depth", 1)
        compare(stack.currentItem, originalNode)
        compare(shutdownSpy.count, 0)
    }

    function test_node_header_peers_back_owns_refresh() {
        const window = createMain(false)
        const stack = findChild(window, "mainPageStack").currentItem
        const originalNode = stack.currentItem
        const refresh = gapSpy(peerRefreshSpy, peerTableModel)
        compare(peerTableModel.autoRefreshActive, false)
        clickVisible(findChild(originalNode, "peersTabButton"))
        tryCompare(stack, "depth", 2)
        tryCompare(stack, "busy", false)
        compare(peerTableModel.autoRefreshActive, true)
        compare(refresh.count, 1)
        clickVisible(findChild(stack.currentItem, "peersBackButton"))
        tryCompare(stack, "depth", 1)
        compare(stack.currentItem, originalNode)
        compare(peerTableModel.autoRefreshActive, false)
        compare(refresh.count, 2)
        compare(shutdownSpy.count, 0)
    }

    function test_node_menu_replaces_peer_child_data() {
        return [
            {tag: "rpc-console", action: "consoleView", section: "rpc-console"},
            {tag: "network-traffic", action: "networkTraffic", section: "network-traffic"},
            {tag: "about", action: "about", section: "about"}
        ]
    }
    function test_node_menu_replaces_peer_child(data) {
        const window = createMain(false)
        const stack = findChild(window, "mainPageStack").currentItem
        const originalNode = stack.currentItem
        const actions = findChild(window, "desktopMenuActions")
        actions.peers.trigger()
        compare(stack.depth, 2)
        compare(peerTableModel.autoRefreshActive, true)
        const refresh = gapSpy(peerRefreshSpy, peerTableModel)
        actions[data.action].trigger()
        compare(stack.depth, 2)
        compare(stack.currentItem.objectName, "settingsView")
        compare(stack.currentItem.selectedSectionId, data.section)
        compare(peerTableModel.autoRefreshActive, false)
        compare(refresh.count, 1)
        actions.nodeView.trigger()
        compare(stack.depth, 1)
        compare(stack.currentItem, originalNode)
        // Replacing an already inactive peer page must not invent a state transition.
        compare(peerTableModel.autoRefreshActive, false)
        compare(refresh.count, 1)
        compare(shutdownSpy.count, 0)
    }

    function test_edit_commands_use_current_visible_target() {
        const window = createMain(true)
        const page = selectedSend(window)
        testRecipientsModel.clearToFront()
        const note = findChild(page, "sendNoteInput")
        verify(note !== null)
        window.requestActivate()
        tryCompare(window, "active", true)
        note.forceActiveFocus()
        tryCompare(note, "activeFocus", true)
        note.cursorPosition = note.text.length
        const original = note.text
        keyClick("x")
        const edited = original + "x"
        compare(note.text, edited)
        const actions = findChild(window, "desktopMenuActions")
        note.selectAll()
        tryCompare(actions.copy, "enabled", true)
        actions.copy.trigger()
        compare(testSetup.nativeClipboardText(), edited)
        actions.undo.trigger()
        compare(note.text, original)
        actions.redo.trigger()
        compare(note.text, edited)
        actions.activityView.trigger()
        findChild(window, "activityTabButton").forceActiveFocus()
        tryCompare(actions.copy, "enabled", false)
        // A queued native intent must resolve the focus target again.
        window.invokeEditCommand("copy")
        compare(testSetup.nativeClipboardText(), edited)
        compare(note.text, edited)
    }

    function test_menu_psbt_completion_cannot_cross_selected_opening() {
        const window = createMain(true)
        testWalletModel.resetSendForTest()
        testSecondWalletModel.resetSendForTest()
        walletController.setOpenWallets([testWalletModel, testSecondWalletModel])
        const desktop = findChild(window, "desktopWalletsPage")
        verify(walletController.selectWallet(testWalletModel))
        const originalView = desktop.activeView
        const original = testWalletModel.psbtActionId
        const replacement = testSecondWalletModel.psbtActionId
        BitcoinUri.holdPublications = true
        try {
            findChild(window, "desktopMenuActions").loadPsbt.trigger()
            compare(originalView.currentTab, WalletView.Send)
            const picker = findChild(originalView.sendPage, "sendPsbtOpenDialog")
            tryCompare(picker, "visible", true)
            const source = picker.contextObject
            verify(source !== null)
            compare(source.originalWallet, testWalletModel)
            compare(source.state, PaymentImportOperation.Empty)
            picker.selectAndAccept("file:///selected-opening.psbt")
            compare(source.selectedPath, "file:///selected-opening.psbt")
            verify(source.pending)
            compare(testWalletModel.psbtActionId, original)
            verify(walletController.selectWallet(testSecondWalletModel))
            const replacementView = desktop.activeView
            verify(replacementView !== originalView)
            BitcoinUri.releasePublications()
            compare(source.state, PaymentImportOperation.Closed)
            compare(desktop.activeView, replacementView)
            compare(testSecondWalletModel.psbtActionId, replacement)
            compare(findChild(originalView.sendPage, "transactionReviewPopup").visible, false)
            compare(findChild(replacementView.sendPage, "transactionReviewPopup").visible, false)
            verify(walletController.selectWallet(testWalletModel))
            compare(findChild(originalView.sendPage, "transactionReviewPopup").visible, false)
            compare(testWalletModel.psbtActionId, original)
        } finally { BitcoinUri.releasePublications() }
    }

    function test_cancel_creation_retains_send_context() {
        const window = createMain(true)
        const page = selectedSend(window)
        const view = findChild(window, "desktopWalletsPage").activeView
        const note = findChild(page, "sendNoteInput")
        note.text = "Retained while choosing a wallet"
        const creations = gapSpy(walletCreatedSpy, walletController)
        findChild(window, "desktopMenuActions").createWallet.trigger()
        const modal = findChild(window, "walletCreationModal")
        tryCompare(modal, "opened", true)
        const navigation = findChild(modal, "walletCreationNavigationBar")
        verify(navigation !== null)
        clickVisible(findChild(navigation, "onboardingCloseButton"))
        tryCompare(modal, "visible", false)
        compare(findChild(window, "mainPageStack").depth, 1)
        compare(findChild(window, "desktopWalletsPage").activeView, view)
        compare(view.sendPage, page)
        compare(findChild(window, "sendTabButton").checked, true)
        compare(note.text, "Retained while choosing a wallet")
        compare(creations.count, 0)
    }

    function test_shutdown_retains_existing_presentation_data() {
        return [
            {tag: "menu-exit", wallet: true, intent: "menu"},
            {tag: "native-close", wallet: true, intent: "close"},
            {tag: "node-back-key", wallet: false, intent: "back"}
        ]
    }
    function test_shutdown_retains_existing_presentation(data) {
        const window = createMain(data.wallet)
        const main = findChild(window, "mainPageStack")
        const originalShell = main.currentItem
        let retained = null
        let note = null
        if (data.wallet) {
            const page = selectedSend(window)
            retained = originalShell.activeView
            note = findChild(page, "sendNoteInput")
            note.text = "Retained until shutdown completes"
        } else {
            retained = originalShell.currentItem
        }
        const actions = findChild(window, "desktopMenuActions")
        if (data.intent === "menu") actions.exit.trigger()
        else if (data.intent === "close") window.close()
        else {
            window.requestActivate()
            tryCompare(window, "active", true)
            main.forceActiveFocus()
            tryCompare(main, "activeFocus", true)
            keyRelease(Qt.Key_Back)
        }
        compare(shutdownSpy.count, 1)
        tryVerify(function() { return findChild(window, "shutdownPage") !== null })
        compare(window.shutdownInProgress, true)
        compare(main.visible, false)
        compare(main.enabled, false)
        compare(main.depth, 1)
        compare(main.currentItem, originalShell)
        compare(data.wallet ? originalShell.activeView : originalShell.currentItem, retained)
        if (data.wallet) compare(note.text, "Retained until shutdown completes")
        compare(actions.settings.enabled, false)
        compare(actions.sendView.enabled, false)
        compare(actions.createWallet.enabled, false)
        actions.settings.trigger()
        actions.sendView.trigger()
        actions.createWallet.trigger()
        compare(shutdownSpy.count, 1)
        compare(main.currentItem, originalShell)
        verify(findChild(window, "createWalletWizard") === null)
    }

    function test_native_close_minimizes_with_tray_policy() {
        const window = createMain(true)
        const page = selectedSend(window)
        const note = findChild(page, "sendNoteInput")
        note.text = "Retained while minimized"
        const shell = findChild(window, "mainPageStack").currentItem
        desktopWindowBehaviorModel.showTrayIcon = true
        desktopWindowBehaviorModel.minimizeToTray = true
        desktopWindowBehaviorModel.minimizeOnClose = true
        compare(desktopTrayIconController.visible, true)
        window.close()
        tryCompare(window, "visibility", Window.Minimized)
        tryCompare(desktopTrayIconController, "windowVisible", false)
        compare(shutdownSpy.count, 0)
        compare(window.shutdownInProgress, false)
        compare(findChild(window, "mainPageStack").currentItem, shell)
        compare(note.text, "Retained while minimized")
    }

    function test_controlled_preferences_count_every_write_without_counting_reads() {
        verify(typeof Controls.Theme.dark === "boolean")
        verify(testSetup.beginControlledPreferences(testCase, false) !== null)
        geometryPreferences = createTemporaryObject(preferencesComponent, testCase)
        verify(geometryPreferences !== null)
        verify(testSetup.settleControlledPreferences())
        compare(testSetup.controlledPreferenceWriteCount(), 0)
        const beforeRead = testSetup.controlledPreferenceOperationId()
        geometryPreferences.refresh()
        verify(testSetup.controlledPreferenceOperationId() > beforeRead)
        compare(testSetup.controlledPreferenceWriteCount(), 0)
        verify(geometryPreferences.requestGeometry(10, 20, 800, 665) > 0)
        compare(testSetup.controlledPreferenceWriteCount(), 1)
        verify(geometryPreferences.requestGeometry(30, 40, 1000, 700) > 0)
        compare(testSetup.controlledPreferenceWriteCount(), 2)
        compare(geometryPreferences.windowX, 0)
        compare(geometryPreferences.windowY, 0)
        verify(testSetup.settleControlledPreferences())
        compare(testSetup.controlledPreferenceWriteCount(), 2)
        compare(geometryPreferences.windowX, 30)
        compare(geometryPreferences.windowY, 40)
        compare(geometryPreferences.windowWidth, 1000)
        compare(geometryPreferences.windowHeight, 700)
        compare(geometryPreferences.error, "")
    }

    function test_window_move_publishes_complete_geometry_after_gui_delivery() {
        if (Qt.platform.pluginName === "minimal") {
            skip("The minimal QPA backend does not emit native position-change signals; this case requires xcb")
            return
        }
        // Keep the singleton attached to its original store before overriding
        // the engine context with a manually driven preferences transport.
        verify(typeof Controls.Theme.dark === "boolean")
        const window = createMain(false, false, false, true, true, function() {
            verify(testSetup.beginControlledPreferences(testCase, false) !== null)
            geometryPreferences = createTemporaryObject(preferencesComponent, testCase)
            verify(geometryPreferences !== null)
            verify(testSetup.settleControlledPreferences())
            compare(geometryPreferences.ready, true)
            verify(geometryPreferences.requestGeometry(40, 50, 800, 665) > 0)
            verify(testSetup.settleControlledPreferences())
        })
        compare(window.visibility, Window.Windowed)
        compare(window.x, 40)
        compare(window.y, 50)
        compare(window.width, 800)
        compare(window.height, 665)
        verify(testSetup.settleControlledPreferences())
        // Native watcher/activation refreshes are reads. Count every distinct
        // public write receipt while retaining the exact two-write oracle.
        const ticket = testSetup.controlledPreferenceWriteCount()
        verify(window.preferenceGeometryRestored)
        compare(geometryPreferences.ready, true)
        const moves = gapSpy(windowXSpy, window)
        window.x = 57
        tryCompare(moves, "count", 1)
        tryVerify(function() { return testSetup.controlledPreferenceWriteCount() > ticket })
        compare(testSetup.controlledPreferenceWriteCount(), ticket + 1)
        window.y = 69
        tryVerify(function() { return testSetup.controlledPreferenceWriteCount() === ticket + 2 })
        compare(geometryPreferences.windowX, 40)
        compare(geometryPreferences.windowY, 50)
        verify(testSetup.runControlledPreferenceWork())
        compare(geometryPreferences.windowX, 40)
        compare(geometryPreferences.windowY, 50)
        verify(testSetup.settleControlledPreferences())
        compare(geometryPreferences.windowX, 57)
        compare(geometryPreferences.windowY, 69)
        compare(geometryPreferences.windowWidth, 800)
        compare(geometryPreferences.windowHeight, 665)
        compare(geometryPreferences.error, "")
    }

    function test_resolved_onboarding_does_not_reopen_creation_after_last_close() {
        const window = createMain(true, true, false, true, true)
        tryCompare(window, "postOnboardingWalletRouteResolved", true)
        const main = findChild(window, "mainPageStack")
        const shell = main.currentItem
        walletController.setOpenWallets([])
        walletController.setWalletLoaded(false)
        walletController.setNoWalletsFound(true)
        compare(window.postOnboardingWalletRouteResolved, true)
        compare(main.currentItem, shell)
        compare(main.depth, 1)
        compare(findChild(window, "blockClockTabButton").checked, true)
        compare(findChild(window, "walletCreationModal").visible, false)
        verify(findChild(window, "createWalletWizard") === null)
        compare(shutdownSpy.count, 0)
    }

    function test_tray_intents_forward_to_owned_effect_data() {
        return [{tag: "show", intent: "show"}, {tag: "hide", intent: "hide"}, {tag: "quit", intent: "quit"}]
    }
    function test_tray_intents_forward_to_owned_effect(data) {
        const window = createMain(false)
        const shell = findChild(window, "mainPageStack").currentItem
        if (data.intent === "show") {
            window.hide()
            tryCompare(desktopTrayIconController, "windowVisible", false)
            const visibility = gapSpy(trayVisibilitySpy, desktopTrayIconController)
            desktopTrayIconController.showRequested()
            compare(desktopTrayIconController.windowVisible, true)
            compare(visibility.count, 1)
            compare(shutdownSpy.count, 0)
        } else if (data.intent === "hide") {
            compare(desktopTrayIconController.windowVisible, true)
            const visibility = gapSpy(trayVisibilitySpy, desktopTrayIconController)
            desktopTrayIconController.hideRequested()
            compare(desktopTrayIconController.windowVisible, false)
            compare(visibility.count, 1)
            compare(shutdownSpy.count, 0)
        } else {
            desktopTrayIconController.quitRequested()
            compare(shutdownSpy.count, 1)
            compare(window.shutdownInProgress, true)
            tryVerify(function() { return findChild(window, "shutdownPage") !== null })
        }
        // The mock measures the native controller handoff/projection. It does
        // not implement the platform window manager's hide/restore behavior.
        compare(findChild(window, "mainPageStack").currentItem, shell)
    }
}

// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.2

import org.bitcoincore.qt 1.0

import "../../qml/components"
import "../../qml/controls"
import "../../qml/pages/wallet" as WalletPages
import "../../qml/pages/node" as NodePages

TestCase {
    id: testCase
    name: "SettingsNavigation"
    when: windowShown
    width: 720
    height: 640

    property int firstCreated: 0
    property int firstDestroyed: 0
    property int pushedCreated: 0
    property int pushedDestroyed: 0
    property int secondCreated: 0
    property int secondDestroyed: 0

    Item {
        id: host
        anchors.fill: parent
    }

    Window {
        id: settingsWindow
        width: 900
        height: 700
        visible: true
    }

    Component { id: displayAmount; BitcoinAmount { unit: optionsModel.displayUnit } }
    Component { id: shortcutConsole; NodePages.CommandConsole { width: 700; height: 600; tabActive: true } }

    Component {
        id: containerComponent

        SettingsPageContainer {
            width: 480
            height: 560
        }
    }

    Component {
        id: sidebarComponent

        SettingsSidebar {
            width: 190
            height: 300
            currentSectionId: "display"
            groupTitles: ({
                "wallet": "Wallet",
                "general": "General",
                "network": "Network"
            })
            model: [
                { id: "wallet", label: "Wallet", group: "wallet" },
                { id: "display", label: "Display", group: "general" },
                { id: "hidden", label: "Hidden", group: "general", visible: false },
                { id: "connection", label: "Connection", group: "network" }
            ]
        }
    }

    Component {
        id: settingsViewComponent

        SettingsView {
            width: 900
            height: 700
            selectedSectionId: "display"
        }
    }

    Component {
        id: walletSettingsStackComponent

        WalletPages.WalletSettingsStack {
            width: 614
            height: 700
            wallet: testWalletModel
        }
    }

    Component {
        id: compactSettingsViewComponent

        SettingsView {
            width: 390
            height: 700
            selectedSectionId: "display"
        }
    }

    Component {
        id: firstPage

        Item {
            objectName: "firstSettingsPage"
            Component.onCompleted: testCase.firstCreated += 1
            Component.onDestruction: testCase.firstDestroyed += 1
        }
    }

    Component {
        id: pushedPage

        Item {
            objectName: "pushedSettingsPage"
            Component.onCompleted: testCase.pushedCreated += 1
            Component.onDestruction: testCase.pushedDestroyed += 1
        }
    }

    Component {
        id: secondPage

        Item {
            objectName: "secondSettingsPage"
            Component.onCompleted: testCase.secondCreated += 1
            Component.onDestruction: testCase.secondDestroyed += 1
        }
    }

    function init() {
        testSetup.failOnWarnings()
        rpcConsoleModel.setCompletionSuggestionsForTest([])
        firstCreated = 0
        firstDestroyed = 0
        pushedCreated = 0
        pushedDestroyed = 0
        secondCreated = 0
        secondDestroyed = 0
        AppMode.walletEnabled = true
        AppMode.isDesktop = true
        nodeModel.mempoolInformationAvailable = true
        Theme.paletteId = "core-dark"
        Theme.blockclocksize = 5 / 12
        tryCompare(Theme, "blockclocksize", 5 / 12)
        optionsModel.resetSettingsEditorsForTest()
        optionsModel.settingsReady = true
        optionsModel.settingsPending = false
        optionsModel.settingsError = ""
        optionsModel.holdSignerPathValidationForTest = false
        optionsModel.finishSignerPathValidationForTest("")
        optionsModel.externalSignerPath = ""
        optionsModel.walletSettingsDirty = false
        optionsModel.displayUnit = 0
        optionsModel.moneyFontChoice = "embedded"
        optionsModel.maxMempoolSizeMB = 300
        optionsModel.storageSettingsDirty = false
        optionsModel.mempoolSettingsDirty = false
        nodeModel.resetMempoolInfoPollingTestState()
        optionsModel.clearCoreSettingStatusesForTest()
        walletController.reset()
        walletController.setWalletLoaded(true)
        walletController.selectedWallet.isEncrypted = false
        const proxySetting = optionsModel.coreSettings.entry("proxy")
        const onionSetting = optionsModel.coreSettings.entry("onion")
        proxySetting.enabled = false
        proxySetting.address = proxySetting.defaultAddress()
        onionSetting.enabled = false
        onionSetting.address = onionSetting.defaultAddress()
        testNetworkTrafficTower.active = false
        testDebugLogModel.active = false
    }

    function readyEditor(view) { tryCompare(view.navigation.editor, "fresh", true) }
    function editSetting(view, key, value) { const input = findChild(view, key + "SettingInput"); verify(input !== null); input.text = value; input.textEdited(); tryCompare(view.navigation.editor.entry(key), "text", value); tryCompare(view.navigation.editor, "inputPending", false) }
    function saveSettings(view) { tryCompare(view.navigation.editor, "canSave", true); const before = view.navigation.editor.saveResult.operationId; findChild(view, "settingsSaveBatchButton").clicked(); tryVerify(() => view.navigation.editor.saveResult.persisted && view.navigation.editor.saveResult.operationId !== before); tryCompare(view.navigation.editor, "busy", false) }

    function publishSettingsDomain() {
        for (let turn = 0; turn < 128; ++turn) {
            if (!optionsModel.stepControlledSettingsDomainForTest()) {
                verify(optionsModel.publishControlledSettingsForTest())
                return
            }
        }
        fail("Controlled settings Domain did not become idle")
    }

    function clickSettingsControl(button, enabled) {
        verify(button !== null)
        tryVerify(() => button.width > 0 && button.height > 0)
        compare(button.enabled, enabled)
        settingsWindow.requestActivate()
        tryCompare(settingsWindow, "active", true)
        for (let item = button; item !== null; item = item.parent) {
            verify(item.visible)
            if (item.clip) {
                const center = button.mapToItem(item, button.width / 2, button.height / 2)
                verify(center.x >= 0 && center.y >= 0 && center.x < item.width && center.y < item.height)
            }
        }
        mouseClick(button, button.width / 2, button.height / 2)
    }

    function clickDoneControl(view, enabled) {
        clickSettingsControl(findChild(view, "settingsDoneButton"), enabled)
    }

    function test_settingsDoneCapturesOnlySafeNavigation_data() {
        return [{ tag: "held-initial-read", state: "loading" },
                { tag: "dirty-consent", state: "dirty" },
                { tag: "held-save-review", state: "saving" }]
    }

    function test_settingsDoneCapturesOnlySafeNavigation(data) {
        verify(optionsModel.beginControlledSettingsForTest())
        let view = null
        let doneCalls = 0
        try {
            view = createTemporaryObject(settingsViewComponent, settingsWindow.contentItem)
            verify(view !== null)
            view.doneClicked.connect(() => { doneCalls += 1 })
            publishSettingsDomain() // Actual initial read is queued but held.
            const editor = view.navigation.editor
            compare(editor.phase, SettingsEditorModel.Loading)
            compare(editor.busy, true)
            compare(editor.dirty, false)
            compare(optionsModel.settingsWritesForTest(), 0)
            if (data.state === "loading") {
                clickDoneControl(view, true)
                compare(doneCalls, 1)
                compare(editor.busy, true) // Navigation did not cancel/finish the read.
                compare(optionsModel.settingsWritesForTest(), 0)
                verify(optionsModel.settleControlledSettingsForTest())
                compare(editor.fresh, true)
            } else {
                verify(optionsModel.settleControlledSettingsForTest())
                compare(editor.fresh, true)
                editor.entry("dbcache").editText("512")
                publishSettingsDomain()
                compare(editor.entry("dbcache").text, "512")
                compare(editor.dirty, true)
                compare(editor.canSave, true)
                if (data.state === "dirty") {
                    clickDoneControl(view, true)
                    publishSettingsDomain()
                    compare(doneCalls, 0)
                    compare(editor.closeQuestion, true)
                    compare(optionsModel.settingsWritesForTest(), 0)
                    editor.closePrompt.answer(TextEditClosePrompt.KeepEditing)
                    publishSettingsDomain()
                    compare(editor.closeQuestion, false)
                    compare(editor.entry("dbcache").text, "512")
                    compare(editor.dirty, true)
                    compare(view.navigation.pending, false)
                } else {
                    editor.save()
                    compare(findChild(view, "settingsDoneButton").enabled, false) // Busy ingress, before Domain.
                    publishSettingsDomain() // Hold the original Core review.
                    compare(editor.phase, SettingsEditorModel.Checking)
                    clickDoneControl(view, false)
                    compare(doneCalls, 0)
                    compare(editor.busy, true)
                    compare(optionsModel.settingsWritesForTest(), 0)
                    verify(optionsModel.settleControlledSettingsForTest())
                    compare(editor.saveResult.persisted, true)
                    compare(editor.saveResult.outcomeKnown, true)
                    compare(optionsModel.settingsWritesForTest(), 1)
                    compare(editor.entry("dbcache").committedText, "512")
                }
            }
        } finally {
            verify(optionsModel.endControlledSettingsForTest())
        }
    }

    function test_displayUnitCommandKeepsCanonicalAmount_data() {
        return [{tag: "BTC", unit: 0}, {tag: "mBTC", unit: 1}, {tag: "bits", unit: 2}, {tag: "sat", unit: 3}]
    }
    function test_displayUnitCommandKeepsCanonicalAmount(data) {
        const amount = createTemporaryObject(displayAmount, host)
        verify(amount !== null)
        amount.satoshi = 123456789
        const view = createTemporaryObject(settingsViewComponent, host)
        verify(view !== null)
        const picker = findChild(view, "displayUnitPicker")
        verify(picker !== null)
        picker.activated(data.unit)
        compare(picker.currentValue, data.unit)
        compare(amount.unit, data.unit)
        compare(amount.satoshi, 123456789)
        optionsModel.displayUnit = 3 - data.unit
        compare(picker.currentValue, 3 - data.unit)
        compare(amount.satoshi, 123456789)
    }
    function test_developerCacheUsesTheSharedTypedBatch() {
        const view = createTemporaryObject(settingsViewComponent, settingsWindow.contentItem)
        verify(view !== null)
        view.selectSection("about"); readyEditor(view)
        const row = findChild(view, "aboutDeveloperRow")
        verify(row !== null); row.clicked()
        tryVerify(() => findChild(view, "settingsDeveloper") !== null)
        const input = findChild(view, "developerDbcacheField")
        verify(input !== null)
        input.text = "1636"; input.textEdited()
        tryCompare(view.navigation.editor.entry("dbcache"), "text", "1636")
        tryCompare(view.navigation.editor, "inputPending", false)
        compare(optionsModel.settingsWritesForTest(), 0)
        saveSettings(view)
        compare(view.navigation.editor.entry("dbcache").nextStartText, "1636")
        compare(optionsModel.settingsWritesForTest(), 1)
    }

    function test_pendingSettingsAndSaveErrorRemainVisible() {
        const view = createTemporaryObject(settingsViewComponent, host)
        verify(view !== null)
        const status = findChild(view, "settingsOperationStatus")
        verify(status !== null)
        compare(status.textStyle, Theme.text.caption)
        optionsModel.settingsPending = true
        tryCompare(view.pageContainer.currentItem, "enabled", false)
        compare(status.text, "Saving settings…")
        optionsModel.settingsError = "The settings file could not be written."
        optionsModel.settingsPending = false
        tryCompare(view.pageContainer.currentItem, "enabled", true)
        compare(status.text, optionsModel.settingsError)
        optionsModel.settingsError = ""
    }

    function test_pendingOptionsOnlyDisableSettingsEditors_data() {
        return [{ tag: "loading", ready: false, pending: false },
                { tag: "saving", ready: true, pending: true }]
    }

    function test_pendingOptionsOnlyDisableSettingsEditors(data) {
        const view = createTemporaryObject(settingsViewComponent, settingsWindow.contentItem,
            { "wallet": testWalletModel })
        verify(view !== null)
        optionsModel.settingsReady = data.ready
        optionsModel.settingsPending = data.pending
        const editableSections = ["external-signer", "display", "window-behavior", "storage", "connection"]
        for (const section of editableSections) {
            view.selectSection(section)
            compare(view.pageContainer.currentItem.enabled, false, section)
        }
        for (const section of ["wallet", "network-traffic", "mempool", "rpc-console", "debug-log", "about"]) {
            view.selectSection(section)
            compare(view.pageContainer.currentItem.enabled, true, section)
            if (section === "mempool") compare(findChild(view, "maxmempoolSettingInput").enabled, false)
        }
        view.selectSection("wallet")
        const walletStack = findChild(view, "walletSettingsStack")
        verify(walletStack !== null)
        walletStack.openPassword(false)
        tryCompare(walletStack, "depth", 2)
        compare(walletStack.currentItem.enabled, true)
        view.selectSection("display")
        compare(view.pageContainer.currentItem.enabled, false)
        optionsModel.settingsReady = true
        optionsModel.settingsPending = false
        tryCompare(view.pageContainer.currentItem, "enabled", true)
        view.selectSection("wallet")
        compare(walletStack.depth, 2)
        compare(walletStack.currentItem.enabled, true)
    }

    function test_pendingSavePreservesPushedProxyDraft() {
        const view = createTemporaryObject(settingsViewComponent, settingsWindow.contentItem, {wallet: testWalletModel})
        view.selectSection("connection"); readyEditor(view)
        const page = view.pageContainer.currentItem
        const input = findChild(page, "proxySettingInput")
        settingsWindow.requestActivate(); tryCompare(settingsWindow, "active", true)
        input.forceActiveFocus(); tryCompare(input, "activeFocus", true)
        input.selectAll(); keyClick(Qt.Key_Backspace)
        const address = "10.0.0.1:9050"
        for (let i = 0; i < address.length; ++i) keyClick(address.charAt(i))
        tryCompare(view.navigation.editor.entry("proxy"), "text", address)
        tryCompare(view.navigation.editor, "dirty", true)
        view.selectSection("display"); tryCompare(view.navigation.editor, "closeQuestion", true)
        compare(view.selectedSectionId, "connection")
        findChild(findChild(view, "settingsCloseQuestion").contentItem, "settingsCloseKeep").clicked(); tryCompare(view.navigation.editor, "closeQuestion", false)
        optionsModel.settingsPending = true
        tryCompare(page, "enabled", false)
        compare(view.navigation.editor.entry("proxy").text, address)
        compare(findChild(view, "settingsSaveBatchButton").enabled, false)
        optionsModel.settingsPending = false; tryCompare(page, "enabled", true)
        compare(view.pageContainer.currentItem, page)
        compare(view.navigation.editor.entry("proxy").text, address)
        saveSettings(view)
        compare(view.navigation.editor.entry("proxy").nextStartText, address)
        compare(optionsModel.settingsWritesForTest(), 1)
        view.selectSection("wallet")
        const walletStack = findChild(view, "walletSettingsStack")
        verify(walletStack !== null); walletStack.openPassword(false)
        tryCompare(walletStack, "depth", 2)
        compare(walletStack.currentItem.enabled, true)
        view.selectSection("connection")
        compare(view.pageContainer.currentItem, page)
        compare(view.navigation.editor.entry("proxy").text, address)
    }

    function finishSignerDiscovery() {
        tryVerify(function() { walletController.advanceSignerForTest(); return !walletController.signers.pending })
    }

    function test_pendingSavePreservesEditorFocus_data() {
        return [
            { tag: "switch-tab", control: "displayHideAmountsRowSwitch", reason: Qt.TabFocusReason, moveFocus: false },
            { tag: "picker-backtab", control: "displayMoneyFontPicker", reason: Qt.BacktabFocusReason, moveFocus: false },
            { tag: "switch-mouse", control: "displayHideAmountsRowSwitch", reason: Qt.MouseFocusReason, moveFocus: false },
            { tag: "focus-moved", control: "displayHideAmountsRowSwitch", reason: Qt.TabFocusReason, moveFocus: true }
        ]
    }

    function test_pendingSavePreservesEditorFocus(data) {
        settingsWindow.requestActivate()
        tryCompare(settingsWindow, "active", true)
        const view = createTemporaryObject(settingsViewComponent, settingsWindow.contentItem)
        readyEditor(view)
        const editor = findChild(view, data.control)
        editor.forceActiveFocus(data.reason)
        tryCompare(editor, "activeFocus", true)
        const visualFocus = editor.visualFocus
        optionsModel.settingsPending = true
        compare(editor.enabled, false)
        const done = findChild(view, "settingsDoneButton")
        if (data.moveFocus) done.forceActiveFocus(Qt.TabFocusReason)
        optionsModel.settingsPending = false
        if (data.moveFocus) {
            tryCompare(done, "activeFocus", true)
            verify(done.visualFocus)
        } else {
            tryCompare(editor, "activeFocus", true)
            compare(editor.visualFocus, visualFocus)
            compare(editor.focusReason, data.reason)
        }
    }

    function test_signerPendingDoesNotShowStaleConnectedDevice() {
        const executable = walletController.signerExecutableForTest()
        walletController.setSignerFixtureForTest(executable, "00000001", "Old device", "")
        finishSignerDiscovery()
        const view = createTemporaryObject(settingsViewComponent, settingsWindow.contentItem)
        view.selectSection("external-signer")
        readyEditor(view)
        const page = view.pageContainer.currentItem
        const input = findChild(page, "externalSignerPathInput")
        const indicator = findChild(page, "externalSignerStatusIndicator")
        const check = findChild(page, "externalSignerCheckDeviceButton")
        finishSignerDiscovery()
        verify(page.signerConnected)
        const initialScans = walletController.signerEnumerationCalls
        const successor = walletController.alternateSignerExecutableForTest()
        verify(successor !== executable)
        input.text = successor
        input.textEdited()
        // Uncommitted text does not replace the effective configuration/device.
        compare(walletController.signers.effectivePath, executable)
        verify(page.signerConnected)
        compare(walletController.signerEnumerationCalls, initialScans)
        tryCompare(check, "text", "Save and check")
        tryCompare(check, "enabled", true)
        clickSettingsControl(check, true)
        tryVerify(() => view.navigation.editor.saveResult.persisted)
        tryVerify(() => view.navigation.editor.saveResult.outcomeKnown)
        compare(optionsModel.settingsWritesForTest(), 1)
        compare(view.navigation.editor.entry("signer").committedText, successor)
        tryVerify(() => walletController.signers.pending)
        compare(page.signerConnected, false)
        tryCompare(indicator, "color", Theme.color.neutral7)
        finishSignerDiscovery()
        compare(walletController.signerConfigurationWrites, 1)
        compare(walletController.signerEnumerationCalls, initialScans + 1)
        compare(walletController.signers.effectivePath, successor)
        verify(page.signerConnected)
        compare(check.text, "Check device")
    }

    function test_pendingSignerInputCannotRefreshTheEffectiveDevice() {
        const executable = walletController.signerExecutableForTest()
        walletController.setSignerFixtureForTest(executable, "00000001", "Original device", "")
        finishSignerDiscovery()
        const view = createTemporaryObject(settingsViewComponent, settingsWindow.contentItem)
        view.selectSection("external-signer"); readyEditor(view); finishSignerDiscovery()
        const page = view.pageContainer.currentItem
        const input = findChild(page, "externalSignerPathInput")
        const check = findChild(page, "externalSignerCheckDeviceButton")
        const scans = walletController.signerEnumerationCalls
        input.text = executable + " --pending-edit"; input.textEdited()
        verify(view.navigation.editor.inputPending)
        compare(check.enabled, false)
        check.clicked()
        tryCompare(view.navigation.editor, "inputPending", false)
        finishSignerDiscovery()
        compare(check.text, "Save and check")
        compare(walletController.signerEnumerationCalls, scans)
        compare(walletController.signerConfigurationWrites, 0)
        compare(walletController.signers.effectivePath, executable)
        compare(optionsModel.settingsWritesForTest(), 0)
    }

    function test_invalidSignerValidationAndFailedSaveDoNotRescan() {
        const executable = walletController.signerExecutableForTest()
        walletController.setSignerFixtureForTest(executable, "00000001", "Old device", "")
        finishSignerDiscovery()
        const view = createTemporaryObject(settingsViewComponent, settingsWindow.contentItem)
        view.selectSection("external-signer")
        readyEditor(view)
        const page = view.pageContainer.currentItem
        const input = findChild(page, "externalSignerPathInput")
        const check = findChild(page, "externalSignerCheckDeviceButton")
        finishSignerDiscovery()
        const initialScans = walletController.signerEnumerationCalls
        input.text = "/missing/r15/hwi"; input.textEdited(); tryCompare(check, "text", "Save and check"); check.clicked()
        finishSignerDiscovery()
        tryVerify(() => view.navigation.editor.error.length > 0)
        verify(findChild(page, "externalSignerEditErrorText").text.indexOf("does not exist") >= 0)
        compare(walletController.signers.effectivePath, executable)
        compare(walletController.signerEnumerationCalls, initialScans)
        input.text = executable + " --different"; input.textEdited()
        optionsModel.rejectSettingsForTest("Save failed.")
        tryCompare(view.navigation.editor.entry("signer"), "text", executable + " --different")
        tryCompare(view.navigation.editor, "inputPending", false)
        tryCompare(check, "text", "Save and check")
        check.clicked(); tryCompare(view.navigation.editor, "error", "Save failed."); finishSignerDiscovery()
        compare(findChild(page, "externalSignerEditErrorText").text, "Save failed.")
        compare(walletController.signers.effectivePath, executable)
        compare(walletController.signerEnumerationCalls, initialScans)
    }

    function test_commandLineSignerRemainsUsableAndCanRefresh() {
        const executable = walletController.signerExecutableForTest()
        walletController.setSignerFixtureForTest(executable, "00000001", "CLI device", "", true)
        finishSignerDiscovery()
        const view = createTemporaryObject(settingsViewComponent, settingsWindow.contentItem)
        view.selectSection("external-signer"); readyEditor(view); finishSignerDiscovery()
        const page = view.pageContainer.currentItem
        verify(findChild(page, "externalSignerPathInput").readOnly)
        verify(page.signerConnected)
        const check = findChild(page, "externalSignerCheckDeviceButton")
        verify(check.enabled)
        const scans = walletController.signerEnumerationCalls
        check.clicked(); finishSignerDiscovery()
        compare(walletController.signerEnumerationCalls, scans + 1)
        compare(walletController.signerConfigurationWrites, 0)
        verify(page.signerStatusText.indexOf("CLI device") >= 0)
    }

    function test_cachedSignerSectionRefreshesOnReturn() {
        const executable = walletController.signerExecutableForTest()
        walletController.setSignerFixtureForTest(executable, "00000001", "Device", "")
        finishSignerDiscovery()
        const view = createTemporaryObject(settingsViewComponent, settingsWindow.contentItem)
        view.selectSection("external-signer"); readyEditor(view); finishSignerDiscovery()
        const page = view.pageContainer.currentItem
        verify(page.signerConnected)
        const scans = walletController.signerEnumerationCalls
        view.selectSection("display")
        walletController.setSignerFixtureForTest(executable, "", "", "", false, false)
        compare(walletController.signerEnumerationCalls, scans)
        view.selectSection("external-signer")
        compare(page.signerConnected, false)
        finishSignerDiscovery()
        compare(walletController.signerEnumerationCalls, scans + 1)
        compare(page.signerConnected, false)
        verify(page.signerStatusText.indexOf("No external signer") >= 0)
    }

    function test_lockedDeviceReasonIsVisibleAndRecoveryDoesNotRequireReentry() {
        const executable = walletController.signerExecutableForTest()
        walletController.setSignerFixtureForTest(executable, "00000001", "Device", "Unlock using promptpin and sendpin.")
        const view = createTemporaryObject(settingsViewComponent, settingsWindow.contentItem)
        view.selectSection("external-signer"); readyEditor(view); finishSignerDiscovery()
        const page = view.pageContainer.currentItem
        compare(page.signerConnected, false)
        compare(page.signerStatusText, "Unlock using promptpin and sendpin.")
        walletController.setSignerFixtureForTest(executable, "00000001", "Device", "", false, false)
        findChild(page, "externalSignerCheckDeviceButton").clicked(); finishSignerDiscovery()
        verify(page.signerConnected)
        compare(page.signerStatusText, "Detected external signer: Device")
    }

    function test_sidebarFiltersGroupsAndEmitsStableSectionId() {
        const sidebar = createTemporaryObject(sidebarComponent, host)
        verify(sidebar !== null)
        compare(sidebar.visibleSections.length, 3)
        verify(findChild(sidebar, "settingsSidebar_wallet") !== null)
        verify(findChild(sidebar, "settingsSidebar_display") !== null)
        verify(findChild(sidebar, "settingsSidebar_hidden") === null)
        const walletGroup = findChild(sidebar, "settingsSidebarGroup_wallet")
        const generalGroup = findChild(sidebar, "settingsSidebarGroup_general")
        const networkGroup = findChild(sidebar, "settingsSidebarGroup_network")
        verify(walletGroup !== null)
        verify(generalGroup !== null)
        verify(networkGroup !== null)
        compare(walletGroup.text, "Wallet")
        compare(generalGroup.text, "General")
        compare(networkGroup.text, "Network")
        compare(walletGroup.font.weight, Font.DemiBold)

        let activatedSection = ""
        sidebar.sectionActivated.connect(function(sectionId) {
            activatedSection = sectionId
        })
        const connection = findChild(sidebar, "settingsSidebar_connection")
        verify(connection !== null)
        connection.clicked()
        compare(activatedSection, "connection")
    }

    function test_settingsViewAppliesRuntimeVisibilityGates() {
        AppMode.walletEnabled = false
        AppMode.isDesktop = false
        nodeModel.mempoolInformationAvailable = false

        const view = createTemporaryObject(settingsViewComponent, settingsWindow.contentItem)
        verify(view !== null)

        compare(view.sectionIsVisible("wallet"), false)
        compare(view.sectionIsVisible("external-signer"), false)
        compare(view.sectionIsVisible("window-behavior"), false)
        compare(view.sectionIsVisible("mempool"), false)
        verify(findChild(view, "settingsSidebar_wallet") === null)
        verify(findChild(view, "settingsSidebar_external-signer") === null)
        verify(findChild(view, "settingsSidebar_window-behavior") === null)
        verify(findChild(view, "settingsSidebar_mempool") === null)

        compare(view.sectionIsVisible("display"), true)
        compare(view.selectedSectionId, "display")
        verify(findChild(view, "settingsSidebar_display") !== null)
    }

    function test_containerLazilyCachesAndRestoresEachSectionStack() {
        const container = createTemporaryObject(containerComponent, host)
        verify(container !== null)

        container.showSection("first", firstPage)
        compare(container.currentSectionId, "first")
        compare(container.depth, 1)
        compare(firstCreated, 1)
        compare(firstDestroyed, 0)

        container.push(pushedPage)
        tryCompare(container, "depth", 2)
        const pushedItem = container.currentItem
        compare(firstDestroyed, 0)
        compare(pushedCreated, 1)
        compare(pushedDestroyed, 0)

        container.showSection("second", secondPage)
        compare(container.currentSectionId, "second")
        compare(container.depth, 1)
        compare(firstDestroyed, 0)
        compare(pushedDestroyed, 0)
        compare(secondCreated, 1)

        container.showSection("first", firstPage)
        compare(container.currentSectionId, "first")
        compare(container.depth, 2)
        compare(container.currentItem, pushedItem)
        compare(firstCreated, 1)
        compare(pushedCreated, 1)
        compare(secondDestroyed, 0)

        container.clear()
        compare(container.depth, 0)
        tryCompare(testCase, "firstDestroyed", 1)
        tryCompare(testCase, "pushedDestroyed", 1)
        tryCompare(testCase, "secondDestroyed", 1)
    }

    function test_selectingCurrentSectionDoesNotReloadItsStack() {
        const container = createTemporaryObject(containerComponent, host)
        verify(container !== null)

        container.showSection("first", firstPage)
        container.push(pushedPage)
        tryCompare(container, "depth", 2)

        container.showSection("first", firstPage)
        compare(container.depth, 2)
        compare(firstCreated, 1)
        compare(firstDestroyed, 0)
        compare(pushedCreated, 1)
        compare(pushedDestroyed, 0)
    }

    function test_settingsViewLazilyCachesSectionsAndIdlesExpensivePages() {
        const view = createTemporaryObject(settingsViewComponent, settingsWindow.contentItem)
        verify(view !== null)
        compare(view.visible, true)
        verify(view.componentForSection("display") !== null)
        compare(view.selectedSectionId, "display")
        tryCompare(view.pageContainer, "depth", 1)
        const displayPage = findChild(view, "displaySettingsPage")
        verify(displayPage !== null)
        verify(findChild(view, "networkTrafficSettingsPage") === null)
        verify(findChild(view, "debugLogView") === null)
        compare(testNetworkTrafficTower.active, false)
        compare(testDebugLogModel.active, false)

        view.selectSection("network-traffic")
        tryCompare(view, "selectedSectionId", "network-traffic")
        compare(findChild(view, "displaySettingsPage"), displayPage)
        const networkTrafficPage = findChild(view, "networkTrafficSettingsPage")
        verify(networkTrafficPage !== null)
        tryCompare(testNetworkTrafficTower, "active", true)
        const networkTrafficHeading = findChild(view, "networkTrafficHeading")
        const networkTrafficDescription = findChild(view, "networkTrafficHeadingDescription")
        const networkTrafficSection = findChild(view, "networkTrafficSection")
        const networkTrafficRangePicker = findChild(view, "networkTrafficRangePicker")
        const networkTrafficReceivedGraph = findChild(view, "networkTrafficReceivedGraph")
        const networkTrafficSentRow = findChild(view, "networkTrafficSentRow")
        const networkTrafficSentGraph = findChild(view, "networkTrafficSentGraph")
        verify(networkTrafficHeading !== null)
        verify(networkTrafficDescription !== null)
        verify(networkTrafficSection !== null)
        verify(networkTrafficRangePicker !== null)
        verify(networkTrafficReceivedGraph !== null)
        verify(networkTrafficSentRow !== null)
        verify(networkTrafficSentGraph !== null)
        compare(networkTrafficHeading.descriptionTextStyle.font.pixelSize,
            Theme.text.description.font.pixelSize)
        compare(networkTrafficDescription.horizontalAlignment, Text.AlignLeft)
        compare(networkTrafficSection.backgroundColor, Theme.color.surface1)
        compare(networkTrafficSentRow.bottomPadding, 16)
        compare(networkTrafficRangePicker.model.length, 4)
        networkTrafficRangePicker.selected(1, networkTrafficRangePicker.model[1])
        tryCompare(testNetworkTrafficTower, "lastFilterWindowSize", 360)

        view.selectSection("debug-log")
        compare(findChild(view, "networkTrafficSettingsPage"), networkTrafficPage)
        tryCompare(testNetworkTrafficTower, "active", false)
        const debugLogPage = findChild(view, "debugLogView")
        verify(debugLogPage !== null)
        tryCompare(testDebugLogModel, "active", true)

        view.selectSection("network-traffic")
        compare(findChild(view, "networkTrafficSettingsPage"), networkTrafficPage)
        compare(findChild(view, "debugLogView"), debugLogPage)
        tryCompare(networkTrafficPage, "trafficGraphScale", 3600)
        tryCompare(testNetworkTrafficTower, "active", true)
        tryCompare(testDebugLogModel, "active", false)

        view.selectSection("debug-log")
        compare(findChild(view, "debugLogView"), debugLogPage)
        tryCompare(testNetworkTrafficTower, "active", false)
        tryCompare(testDebugLogModel, "active", true)

        view.selectSection("about")
        tryCompare(testDebugLogModel, "active", false)
        verify(findChild(view, "aboutSettingsPage") !== null)
        compare(findChild(view, "debugLogView"), debugLogPage)

        view.visible = false
        compare(view.pageContainer.depth, 1)
        tryCompare(testDebugLogModel, "active", false)
        tryCompare(testNetworkTrafficTower, "active", false)
        compare(findChild(view, "debugLogView"), debugLogPage)
        compare(findChild(view, "networkTrafficSettingsPage"), networkTrafficPage)

        view.visible = true
        compare(view.pageContainer.depth, 1)
        verify(findChild(view, "aboutSettingsPage") !== null)
        compare(findChild(view, "debugLogView"), debugLogPage)
        compare(findChild(view, "networkTrafficSettingsPage"), networkTrafficPage)
        tryCompare(testDebugLogModel, "active", false)
        tryCompare(testNetworkTrafficTower, "active", false)
    }

    function test_walletSettingsStackPreservesAddressStackWhileHidden() {
        const stack = createTemporaryObject(walletSettingsStackComponent, settingsWindow.contentItem)
        verify(stack !== null)

        stack.openAddressHistory()
        tryCompare(stack, "depth", 2)
        const addressPage = stack.currentItem
        compare(addressPage.objectName, "addressListPage")
        compare(addressPage.wallet, testWalletModel)

        stack.visible = false
        compare(stack.depth, 2)
        compare(stack.currentItem, addressPage)

        stack.visible = true
        compare(stack.depth, 2)
        compare(stack.currentItem, addressPage)
    }

    function test_walletSettingsStackPasswordPageFollowsEncryption() {
        const stack = createTemporaryObject(walletSettingsStackComponent, settingsWindow.contentItem)
        verify(stack !== null)

        const passwordRow = findChild(stack, "walletPasswordRow")
        verify(passwordRow !== null)
        compare(passwordRow.title, "Set password")
        passwordRow.clicked()
        tryCompare(stack, "depth", 2)
        compare(stack.currentItem.objectName, "walletPasswordSettingsPage")
        compare(stack.currentItem.updating, false)
        compare(stack.currentItem.wallet, testWalletModel)

        stack.resetToRoot()
        compare(stack.depth, 1)
        testWalletModel.isEncrypted = true
        compare(passwordRow.title, "Update password")
        passwordRow.clicked()
        tryCompare(stack, "depth", 2)
        compare(stack.currentItem.updating, true)
    }

    function test_settingsViewHostsProvidedWalletSettings() {
        const view = createTemporaryObject(settingsViewComponent, settingsWindow.contentItem)
        verify(view !== null)
        compare(view.walletSettingsHost, null)

        view.selectSection("wallet")
        verify(view.walletSettingsHost !== null)
        compare(view.walletSettingsHost.objectName, "walletSettingsHost")
        const emptyHeading = findChild(view, "walletSettingsEmptyHeading")
        verify(emptyHeading !== null)
        compare(emptyHeading.visible, true)

        view.walletSettingsProvided = true
        compare(emptyHeading.visible, false)
    }

    // The node pages (used on Android) have no wallet shell, so the settings
    // view shows the given wallet's settings itself.
    function test_settingsViewShowsGivenWalletWithoutShell() {
        const view = createTemporaryObject(settingsViewComponent, settingsWindow.contentItem,
            { "wallet": testWalletModel })
        verify(view !== null)
        view.selectSection("wallet")
        const stack = findChild(view, "walletSettingsStack")
        verify(stack !== null)
        compare(stack.wallet, testWalletModel)
        compare(stack.currentItem.objectName, "walletSettingsPage")
        compare(stack.currentItem.walletLoaded, true)

        stack.openAddressHistory()
        tryCompare(stack, "depth", 2)
        findChild(view, "settingsSidebar_wallet").clicked()
        compare(stack.depth, 1)

        view.wallet = null
        tryVerify(function() { return findChild(view, "walletSettingsStack") === null })
        compare(findChild(view, "walletSettingsEmptyHeading").visible, true)
    }

    // A wallet settings stack keeps one wallet for its life: switching the
    // host's wallet replaces the stack, its pages and their controllers, and
    // returning creates a new, idle page.
    function test_switchingTheHostWalletRecreatesItsStack() {
        const view = createTemporaryObject(settingsViewComponent, settingsWindow.contentItem,
            { "wallet": testWalletModel })
        verify(view !== null)
        view.selectSection("wallet")
        const first = findChild(view, "walletSettingsStack")
        verify(first !== null)
        const firstBackup = findChild(first, "walletBackupController")
        verify(firstBackup !== null)
        compare(firstBackup.backups, testWalletModel.backups)
        first.openAddressHistory()
        tryCompare(first, "depth", 2)

        view.wallet = testSecondWalletModel
        tryVerify(function() { const stack = findChild(view, "walletSettingsStack"); return stack !== null && stack !== first })
        const second = findChild(view, "walletSettingsStack")
        compare(second.wallet, testSecondWalletModel)
        compare(second.depth, 1)
        compare(findChild(second, "walletBackupController").backups, testSecondWalletModel.backups)

        view.wallet = testWalletModel
        tryVerify(function() { const stack = findChild(view, "walletSettingsStack"); return stack !== null && stack !== second })
        const again = findChild(view, "walletSettingsStack")
        compare(again.wallet, testWalletModel)
        compare(again.depth, 1)
        compare(findChild(again, "walletBackupController").operation, null)
    }

    function test_walletSettingsEmptyStateMatchesWalletAvailability() {
        const view = createTemporaryObject(settingsViewComponent, settingsWindow.contentItem)
        verify(view !== null)
        view.selectSection("wallet")
        walletController.setWalletLoaded(false)

        const heading = findChild(view, "walletSettingsEmptyHeading")
        const title = findChild(view, "walletSettingsEmptyHeadingTitle")
        const action = findChild(view, "walletSettingsEmptyAction")
        verify(heading !== null)
        verify(title !== null)
        verify(action !== null)
        compare(title.horizontalAlignment, Text.AlignHCenter)

        walletController.noWalletsFound = true
        compare(heading.title, "")
        compare(title.visible, false)
        compare(heading.description, "Add a wallet to manage its settings.")
        compare(action.text, "Add wallet…")

        walletController.noWalletsFound = false
        compare(heading.title, "No wallet selected")
        compare(title.visible, true)
        compare(heading.description, "Select a wallet to manage wallet-specific settings.")
        compare(action.text, "Select wallet…")
    }

    function test_settingsViewPinsSidebarAndLetsPageContainerGrow() {
        const view = createTemporaryObject(settingsViewComponent, settingsWindow.contentItem)
        verify(view !== null)

        const sidebarSurface = findChild(view, "settingsSidebarSurface")
        const sidebarHeading = findChild(view, "settingsSidebarHeading")
        const displayPage = findChild(view, "displaySettingsPage")
        verify(sidebarSurface !== null)
        verify(sidebarHeading !== null)
        verify(displayPage !== null)

        tryCompare(sidebarSurface, "x", 0)
        tryCompare(sidebarSurface, "width", view.sidebarWidth)
        compare(sidebarSurface.color, Theme.color.surface1)
        compare(sidebarHeading.text, "Settings")
        compare(sidebarHeading.font.pixelSize, Theme.text.display.font.pixelSize)
        compare(sidebarHeading.horizontalAlignment, Text.AlignLeft)
        const pageColumn = findChild(view, "navigationSplitDetail")
        verify(pageColumn !== null)
        tryCompare(pageColumn, "x", view.sidebarWidth)
        tryCompare(view.pageContainer, "width", view.width - view.sidebarWidth)
        compare(findChild(view, "navigationSplitSeparator").visible, false)

        verify(displayPage.contentHorizontalPadding >= Theme.space.pageCompact)
        verify(displayPage.contentLayout.width <= displayPage.maximumContentWidth)
        verify(displayPage.contentLayout.width < view.pageContainer.width)
    }

    function test_compactSettingsShowOneColumnWithBackToTheSidebar() {
        const view = createTemporaryObject(compactSettingsViewComponent, settingsWindow.contentItem)
        verify(view !== null)
        const split = findChild(view, "settingsSplitView")
        const sidebarColumn = findChild(view, "navigationSplitPrimary")
        const pageColumn = findChild(view, "navigationSplitDetail")
        const sidebarSurface = findChild(view, "settingsSidebarSurface")
        verify(split !== null)
        verify(sidebarSurface !== null)

        compare(view.compact, true)
        compare(split.compactColumn, NavigationSplitView.Primary)
        compare(sidebarColumn.x, 0)
        compare(sidebarSurface.width, 390)
        compare(pageColumn.x, 390)
        compare(pageColumn.visible, false)
        compare(view.sidebar.currentSectionId, "")

        view.sidebar.sectionActivated("connection")
        tryCompare(pageColumn, "x", 0)
        compare(pageColumn.visible, true)
        compare(view.pageContainer.width, 390)
        compare(sidebarColumn.x, -390)
        compare(sidebarColumn.visible, false)
        const connectionPage = findChild(view, "connectionSettingsPage")
        verify(connectionPage !== null)
        compare(connectionPage.showBackButton, true)

        const back = findChild(connectionPage, "settingsSectionBackButton")
        verify(back !== null)
        back.clicked()
        compare(split.compactColumn, NavigationSplitView.Primary)
        tryCompare(pageColumn, "x", 390)
        compare(sidebarColumn.x, 0)
        compare(view.selectedSectionId, "connection")

        view.openWalletSettings()
        compare(split.compactColumn, NavigationSplitView.Detail)
        compare(view.selectedSectionId, "wallet")

        view.width = 900
        compare(view.compact, false)
        tryCompare(pageColumn, "x", view.sidebarWidth)
        compare(sidebarSurface.width, view.sidebarWidth)
        compare(connectionPage.showBackButton, false)
        compare(view.sidebar.currentSectionId, "wallet")

        // Narrowing shows the open section's page, even when the list was
        // the last compact column.
        split.showPrimary()
        view.selectSection("display")
        view.width = 390
        compare(split.compactColumn, NavigationSplitView.Detail)
        compare(pageColumn.x, 0)
        compare(view.pageContainer.currentSectionId, "display")
    }

    function test_dataHeavyPagesUseAvailableWidthWithResponsivePadding() {
        const view = createTemporaryObject(settingsViewComponent, settingsWindow.contentItem)
        verify(view !== null)
        view.width = 1500

        view.selectSection("network-traffic")
        const networkTrafficPage = findChild(view, "networkTrafficSettingsPage")
        verify(networkTrafficPage !== null)
        verify(networkTrafficPage.contentHorizontalPadding >= Theme.space.pageCompact)
        tryCompare(networkTrafficPage.contentLayout, "width",
            networkTrafficPage.scrollView.availableWidth
                - networkTrafficPage.contentHorizontalPadding * 2)
        verify(networkTrafficPage.contentLayout.width > 840)

        view.selectSection("debug-log")
        const debugLogPage = findChild(view, "debugLogView")
        const debugLogContent = findChild(view, "debugLogContentLayout")
        verify(debugLogPage !== null)
        verify(debugLogContent !== null)
        verify(debugLogPage.contentHorizontalPadding >= Theme.space.pageCompact)
        tryCompare(debugLogContent, "width",
            debugLogPage.width - debugLogPage.contentHorizontalPadding * 2)
        verify(debugLogContent.width > 840)
    }

    function test_settingsViewGroupsRelatedDestinations() {
        const view = createTemporaryObject(settingsViewComponent, settingsWindow.contentItem)
        verify(view !== null)

        compare(view.sectionForId("wallet").label, "Wallet settings")
        compare(view.sectionForId("wallet").group, "wallet")
        compare(view.sectionForId("storage").group, "general")
        compare(view.sectionForId("network-traffic").group, "network")
        compare(view.sectionForId("mempool").group, "advanced")
        compare(view.sectionForId("rpc-console").group, "advanced")
        compare(view.sectionForId("debug-log").group, "advanced")
        compare(view.groupTitles.wallet, "Wallet")
        compare(view.groupTitles.general, "General")
        compare(view.groupTitles.network, "Network")
        compare(view.groupTitles.advanced, "Advanced")
    }

    function test_rpc_namespace_is_independent_of_canonical_presentation_identity() {
        const oldName = testWalletModel.name
        try {
            testWalletModel.name = "savings"
            testWalletModel.setRpcNamespace("savings/")
            const view = createTemporaryObject(settingsViewComponent, settingsWindow.contentItem, { wallet: testWalletModel })
            view.selectSection("rpc-console")
            const page = findChild(view, "rpcConsoleSettingsPage")
            verify(page !== null)
            compare(view.activeWalletName, "savings")
            compare(page.walletName, "savings/")
            compare(page.walletContext, true)
            const input = findChild(page, "consoleInput")
            verify(input !== null)
            input.text = "getwalletinfo"
            page.consoleItem.submitCommand()
            compare(rpcConsoleModel.lastTarget, "wallet")
            compare(rpcConsoleModel.lastWalletName, "savings/")
            compare(testWalletModel.name, "savings")
        } finally {
            testWalletModel.clearRpcNamespace()
            testWalletModel.name = oldName
        }
    }

    function test_rpc_context_distinguishes_an_unnamed_wallet() {
        const oldName = testWalletModel.name
        const oldAvailable = testWalletModel.available
        try {
            testWalletModel.name = ""
            testWalletModel.available = true
            const view = createTemporaryObject(settingsViewComponent, settingsWindow.contentItem,
                { wallet: testWalletModel })
            view.selectSection("rpc-console")
            const page = findChild(view, "rpcConsoleSettingsPage")
            verify(page !== null)
            const input = findChild(page, "consoleInput")
            verify(input !== null)
            compare(page.walletContext, true)
            compare(page.walletName, "")
            input.text = "getwalletinfo"
            page.consoleItem.submitCommand()
            compare(rpcConsoleModel.lastTarget, "wallet")
            compare(rpcConsoleModel.lastWalletName, "")
            view.wallet = null
            compare(page.walletContext, false)
            page.consoleItem.submitCommand()
            compare(rpcConsoleModel.lastTarget, "node")
        } finally {
            testWalletModel.name = oldName
            testWalletModel.available = oldAvailable
        }
    }

    function test_unavailable_rpc_wallet_never_becomes_a_node_request() {
        const oldAvailable = testWalletModel.available
        try {
            testWalletModel.available = true
            const view = createTemporaryObject(settingsViewComponent, settingsWindow.contentItem,
                { wallet: testWalletModel })
            view.selectSection("rpc-console")
            const page = findChild(view, "rpcConsoleSettingsPage")
            verify(page !== null)
            const input = findChild(page, "consoleInput")
            verify(input !== null)
            input.text = "getwalletinfo"
            const before = rpcConsoleModel.commandCount
            testWalletModel.available = false
            compare(page.walletContext, true)
            compare(page.targetAvailable, false)
            page.consoleItem.submitCommand()
            compare(rpcConsoleModel.commandCount, before)
            compare(input.text, "getwalletinfo")
            testWalletModel.available = true
            compare(page.targetAvailable, true)
            page.consoleItem.submitCommand()
            compare(rpcConsoleModel.commandCount, before + 1)
            compare(rpcConsoleModel.lastTarget, "wallet")
        } finally {
            testWalletModel.available = oldAvailable
        }
    }

    function test_desktop_rpc_endpoint_kind_is_explicit_data() {
        return [
            { tag: "named", label: "Friendly wallet", rpc: "original/", available: true },
            { tag: "default", label: "", rpc: "", available: true },
            { tag: "unavailable", label: "Original wallet", rpc: "original/", available: false }
        ]
    }

    function test_desktop_rpc_endpoint_kind_is_explicit(data) {
        // DesktopWallets supplies scalar endpoint facts while its wallet
        // settings stack is hosted separately. No name can encode endpoint kind.
        const view = createTemporaryObject(settingsViewComponent, host, {
            activeWalletName: data.label, activeWalletRpcName: data.rpc,
            activeWalletRpcContext: true, activeWalletRpcReady: true,
            activeWalletAvailable: data.available
        })
        compare(view.wallet, null)
        view.selectSection("rpc-console")
        const page = findChild(view, "rpcConsoleSettingsPage")
        verify(page !== null)
        compare(page.walletContext, true)
        compare(page.walletName, data.rpc)
        compare(page.targetAvailable, data.available)
        const input = findChild(page, "consoleInput")
        const before = rpcConsoleModel.commandCount
        input.text = "getwalletinfo"
        page.consoleItem.submitCommand()
        compare(rpcConsoleModel.commandCount, before + (data.available ? 1 : 0))
        if (data.available) {
            compare(rpcConsoleModel.lastTarget, "wallet")
            compare(rpcConsoleModel.lastWalletName, data.rpc)
        } else {
            compare(input.text, "getwalletinfo")
            input.text = "stop"
            page.consoleItem.submitCommand()
            compare(rpcConsoleModel.commandCount, before + 1)
            compare(rpcConsoleModel.lastTarget, "node")
            compare(page.walletContext, true)
            compare(page.walletName, data.rpc)
        }
    }

    function test_clear_shortcut_preserves_draft_with_native_autocomplete_data() {
        return [ { tag: "ordinary", popup: false }, { tag: "autocomplete", popup: true } ]
    }

    function test_clear_shortcut_preserves_draft_with_native_autocomplete(data) {
        const window = testCase.Window.window
        const console = createTemporaryObject(shortcutConsole, window.contentItem)
        verify(console !== null)
        compare(console.Window.window, window)
        verify(console.visible)
        const input = findChild(console, "consoleInput")
        const popup = findChild(console, "consoleAutocompletePopup")
        verify(input !== null); verify(popup !== null)
        verify(input.visible)
        window.requestActivate()
        tryCompare(window, "active", true)
        verify(testSetup.replaceText(input, "getblock"))
        tryCompare(input, "activeFocus", true)
        const beforeCommands = rpcConsoleModel.commandCount
        const beforeClear = rpcConsoleModel.clearCount
        const modifier = Qt.platform.os === "osx" ? Qt.MetaModifier : Qt.ControlModifier
        try {
            rpcConsoleModel.setCompletionSuggestionsForTest(data.popup ? ["getblockcount", "getblockhash"] : [])
            tryCompare(popup, "opened", data.popup)
            keyClick(Qt.Key_L, modifier)
            tryCompare(rpcConsoleModel, "clearCount", beforeClear + 1)
            compare(rpcConsoleModel.commandCount, beforeCommands)
            compare(input.text, "getblock")
            if (data.popup) {
                keyClick(Qt.Key_Escape)
                tryCompare(popup, "visible", false)
                compare(input.activeFocus, true)
                compare(input.text, "getblock")
                compare(rpcConsoleModel.commandCount, beforeCommands)
                keyClick(Qt.Key_L, modifier)
                tryCompare(rpcConsoleModel, "clearCount", beforeClear + 2)
                compare(rpcConsoleModel.commandCount, beforeCommands)
                compare(input.text, "getblock")
            }
        } finally {
            rpcConsoleModel.setCompletionSuggestionsForTest([])
            popup.close()
        }
    }

    function test_rpcConsoleUsesSettingsContainerAndTracksVisibility() {
        const view = createTemporaryObject(settingsViewComponent, settingsWindow.contentItem)
        verify(view !== null)
        view.selectSection("rpc-console")

        const page = findChild(view, "rpcConsoleSettingsPage")
        const header = findChild(view, "rpcConsoleHeader")
        const heading = findChild(view, "rpcConsolePageHeading")
        const warning = findChild(view, "rpcConsoleWarningBanner")
        const warningCloseButton = findChild(view, "rpcConsoleWarningBannerCloseButton")
        const toolbar = findChild(view, "rpcConsoleToolbar")
        const searchBar = findChild(view, "rpcConsoleSearchBar")
        const search = findChild(view, "rpcConsoleSearchField")
        const searchIcon = findChild(view, "rpcConsoleSearchIcon")
        const searchClearButton = findChild(view, "rpcConsoleSearchClearButton")
        const searchNavigation = findChild(view, "rpcConsoleSearchNavigation")
        const previousSearchButton = findChild(view, "rpcConsoleSearchPreviousButton")
        const nextSearchButton = findChild(view, "rpcConsoleSearchNextButton")
        const previousSearchFocusBorder = findChild(view, "rpcConsoleSearchPreviousButtonFocusBorder")
        const nextSearchFocusBorder = findChild(view, "rpcConsoleSearchNextButtonFocusBorder")
        const previousSearchIcon = findChild(view, "rpcConsoleSearchPreviousButtonIcon")
        const nextSearchIcon = findChild(view, "rpcConsoleSearchNextButtonIcon")
        const fontStepper = findChild(view, "consoleFontStepper")
        const decreaseButton = findChild(view, "consoleFontDecreaseButton")
        const increaseButton = findChild(view, "consoleFontIncreaseButton")
        const decreaseFocusBorder = findChild(view, "consoleFontDecreaseButtonFocusBorder")
        const increaseFocusBorder = findChild(view, "consoleFontIncreaseButtonFocusBorder")
        const decreaseLabel = findChild(view, "consoleFontDecreaseButtonLabel")
        const increaseLabel = findChild(view, "consoleFontIncreaseButtonLabel")
        const rpcConsole = findChild(view, "rpcConsole")
        const commandInputRow = findChild(view, "consoleInputRow")
        const commandInputDivider = findChild(view, "consoleInputDivider")
        const commandInput = findChild(view, "consoleInput")
        const helpFooter = findChild(view, "rpcConsoleHelpFooter")
        verify(page !== null)
        verify(header !== null)
        verify(heading !== null)
        verify(warning !== null)
        verify(warningCloseButton !== null)
        verify(toolbar !== null)
        verify(searchBar !== null)
        verify(search !== null)
        verify(searchIcon !== null)
        verify(searchClearButton !== null)
        verify(searchNavigation !== null)
        verify(previousSearchButton !== null)
        verify(nextSearchButton !== null)
        verify(previousSearchFocusBorder !== null)
        verify(nextSearchFocusBorder !== null)
        verify(previousSearchIcon !== null)
        verify(nextSearchIcon !== null)
        verify(fontStepper !== null)
        verify(decreaseButton !== null)
        verify(increaseButton !== null)
        verify(decreaseFocusBorder !== null)
        verify(increaseFocusBorder !== null)
        verify(decreaseLabel !== null)
        verify(increaseLabel !== null)
        verify(rpcConsole !== null)
        verify(commandInputRow !== null)
        verify(commandInputDivider !== null)
        verify(commandInput !== null)
        verify(helpFooter !== null)
        compare(header.title, "RPC console")
        compare(header.showBackButton, false)
        compare(page.maximumContentWidth, page.width)
        verify(page.contentHorizontalPadding >= Theme.space.pageCompact)
        compare(heading.description, "Execute RPC commands and inspect their responses.")
        compare(warning.text,
                "Beware of scammers who may ask you to enter commands here to steal your funds. Only enter commands you fully understand.")
        tryCompare(warning, "opacity", 1)
        compare(search.placeholderText, "Search console")
        search.text = "help"
        compare(rpcConsole.searchText, "help")
        compare(searchBar.height, 48)
        compare(search.height, 40)
        compare(searchBar.background.visible, true)
        compare(searchBar.background.color, Theme.color.surface1)
        compare(search.background.color, Theme.color.surface2)
        compare(searchIcon.source.toString(), "image://images/search")
        compare(searchClearButton.visible, true)
        compare(previousSearchIcon.width, 14)
        compare(previousSearchIcon.height, 14)
        compare(nextSearchIcon.width, 14)
        compare(nextSearchIcon.height, 14)
        compare(previousSearchIcon.strokeWidth, 2)
        compare(nextSearchIcon.strokeWidth, 2)
        compare(previousSearchIcon.strokeColor, Theme.color.disabledContent)
        compare(nextSearchIcon.strokeColor, Theme.color.disabledContent)
        compare(previousSearchIcon.rotation, -90)
        compare(nextSearchIcon.rotation, 90)
        compare(previousSearchButton.enabled, false)
        compare(nextSearchButton.enabled, false)
        verify(search.mapToItem(searchBar, 0, 0).x
               < previousSearchButton.mapToItem(searchBar, 0, 0).x)
        verify(previousSearchButton.mapToItem(searchBar, 0, 0).x
               < nextSearchButton.mapToItem(searchBar, 0, 0).x)
        verify(searchBar.x < fontStepper.x)
        verify(heading.y < warning.y)
        verify(warning.y < toolbar.y)
        verify(toolbar.y < rpcConsole.y)
        verify(helpFooter.y >= rpcConsole.y + rpcConsole.height)
        compare(helpFooter.horizontalAlignment, Text.AlignHCenter)
        compare(helpFooter.mapToItem(page, helpFooter.width / 2, 0).x,
                rpcConsole.mapToItem(page, rpcConsole.width / 2, 0).x)
        compare(helpFooter.text,
                "Use ↑↓ arrows to navigate history. Type help for an overview of available commands. Type help-console for console syntax help.")
        compare(commandInput.placeholderText, "Enter command…")
        compare(commandInputRow.color, Theme.color.surface1)
        compare(commandInput.background.color, Theme.color.surface2)
        compare(commandInputDivider.height, 1)
        compare(commandInputDivider.color, Theme.color.surfaceDivider)
        compare(rpcConsole.showHeader, false)
        compare(rpcConsole.tabActive, true)
        compare(rpcConsole.outputFontPixelSize, 13)
        compare(fontStepper.width, 72)
        compare(decreaseLabel.text, "A")
        compare(increaseLabel.text, "A")
        verify(decreaseLabel.font.pixelSize < increaseLabel.font.pixelSize)

        settingsWindow.requestActivate()
        tryCompare(settingsWindow, "active", true)
        // The console focuses its input in a deferred call once shown; let it run first.
        wait(0)
        searchBar.navigationEnabled = true
        search.forceActiveFocus()
        tryCompare(search, "activeFocus", true)
        keyClick(Qt.Key_Tab)
        tryCompare(previousSearchButton, "activeFocus", true)
        // FocusBorder animates visibility, so wait for both showing and hiding.
        tryCompare(previousSearchFocusBorder, "visible", true)
        compare(previousSearchFocusBorder.border.color, Theme.color.focus)
        tryCompare(nextSearchFocusBorder, "visible", false)
        keyClick(Qt.Key_Tab)
        tryCompare(nextSearchButton, "activeFocus", true)
        tryCompare(nextSearchFocusBorder, "visible", true)
        compare(nextSearchFocusBorder.border.color, Theme.color.focus)
        tryCompare(previousSearchFocusBorder, "visible", false)
        keyClick(Qt.Key_Tab)
        tryCompare(decreaseButton, "activeFocus", true)
        tryCompare(decreaseFocusBorder, "visible", true)
        compare(decreaseFocusBorder.border.color, Theme.color.focus)
        tryCompare(increaseFocusBorder, "visible", false)
        keyClick(Qt.Key_Tab)
        tryCompare(increaseButton, "activeFocus", true)
        tryCompare(increaseFocusBorder, "visible", true)
        compare(increaseFocusBorder.border.color, Theme.color.focus)
        tryCompare(decreaseFocusBorder, "visible", false)

        mouseClick(increaseButton)
        compare(rpcConsole.outputFontPixelSize, 14)
        mouseClick(decreaseButton)
        compare(rpcConsole.outputFontPixelSize, 13)
        const consoleHeightBeforeDismiss = rpcConsole.height
        // In its layout: a wide font makes the page scroll.
        const footerBottomBeforeDismiss = helpFooter.y + helpFooter.height
        page.scrollArea.contentY = 0
        mouseClick(warningCloseButton)
        tryCompare(warning, "visible", false)
        compare(page.warningVisible, false)
        tryCompare(rpcConsole, "height",
                   consoleHeightBeforeDismiss + warning.implicitHeight + page.contentSpacing)
        tryVerify(function() {
            return Math.abs(helpFooter.y + helpFooter.height - footerBottomBeforeDismiss) < 0.5
        })

        view.selectSection("about")
        tryCompare(rpcConsole, "tabActive", false)
    }

    function test_settings_scroll_to_keyboard_focus() {
        const originalHeight = settingsWindow.height
        settingsWindow.height = 420
        try {
            const view = createTemporaryObject(settingsViewComponent, settingsWindow.contentItem)
            verify(waitForRendering(view))
            const row = findChild(view, "displayTransactionUrlsRow")
            const page = view.pageContainer.currentItem
            const viewport = page.scrollView.contentItem
            row.forceActiveFocus(Qt.TabFocusReason)
            tryVerify(function() {
                const y = row.mapToItem(viewport, 0, 0).y
                return y >= 0 && y + row.height <= viewport.height + 1
            })
        } finally {
            settingsWindow.height = originalHeight
        }
    }

    function test_aboutDeveloperOptionsRequireLiveSettingsModel() {
        compare(ApplicationModels.optionsModel, optionsModel)
        compare(ApplicationModels.onboardingOptionsModel, null)
        const view = createTemporaryObject(settingsViewComponent, settingsWindow.contentItem)
        verify(view !== null)
        view.selectSection("about")
        const developerRow = findChild(view, "aboutDeveloperRow")
        verify(developerRow !== null)
        compare(developerRow.visible, true)
        compare(developerRow.enabled, true)
        developerRow.clicked()
        const developerPage = findChild(view, "settingsDeveloper")
        verify(developerPage !== null)
        const dbcacheField = findChild(developerPage, "developerDbcacheField")
        const scriptThreadsField = findChild(developerPage, "developerScriptThreadsField")
        verify(dbcacheField !== null)
        verify(scriptThreadsField !== null)
        // The settings editor supplies these values; see test_developerCacheUsesTheSharedTypedBatch.
    }

    function test_displayPageUsesInlineGenericPickers() {
        const view = createTemporaryObject(settingsViewComponent, settingsWindow.contentItem)
        verify(view !== null)

        const themePicker = findChild(view, "displayThemePicker")
        const blockStatusSizePicker = findChild(view, "displayBlockStatusSizePicker")
        const moneyFontPicker = findChild(view, "displayMoneyFontPicker")
        const displayUnitPicker = findChild(view, "displayUnitPicker")
        const languageDisclosure = findChild(view, "displayLanguageDisclosureIndicator")
        const developerSection = findChild(view, "displayDeveloperSection")
        const designSystemRow = findChild(view, "displayDesignSystemRow")

        verify(themePicker !== null)
        verify(blockStatusSizePicker !== null)
        verify(moneyFontPicker !== null)
        verify(displayUnitPicker !== null)
        verify(languageDisclosure !== null)
        verify(developerSection !== null)
        verify(designSystemRow !== null)
        compare(languageDisclosure.size, 14)
        compare(developerSection.visible, BuildInfo.isDebug)
        compare(displayUnitPicker.subtitleRole, "description")
        compare(displayUnitPicker.minimumMenuWidth, 400)
        tryVerify(function() { return displayUnitPicker.itemAtIndex(3) !== null })
        compare(displayUnitPicker.itemAtIndex(0).subtitle,
            "8 decimal places (0.00000001 BTC = 1 sat)")
        compare(displayUnitPicker.itemAtIndex(1).subtitle,
            "5 decimal places (0.00001 mBTC = 1 sat)")
        compare(displayUnitPicker.itemAtIndex(2).subtitle,
            "2 decimal places (0.01 bits = 1 sat)")
        compare(displayUnitPicker.itemAtIndex(3).subtitle,
            "Satoshi, the smallest unit (1 sat = 0.00000001 BTC)")

        themePicker.selected(0, "Light")
        tryCompare(Theme, "dark", false)

        compare(blockStatusSizePicker.subtitleRole, "description")
        compare(blockStatusSizePicker.iconRole, "icon")
        compare(blockStatusSizePicker.iconSize, 40)
        compare(blockStatusSizePicker.minimumMenuWidth, 520)
        compare(blockStatusSizePicker.currentText, "Compact")
        tryVerify(function() { return blockStatusSizePicker.itemAtIndex(1) !== null })
        compare(blockStatusSizePicker.itemAtIndex(0).subtitle,
            "For personal use on a computer or smartphone.")
        compare(blockStatusSizePicker.itemAtIndex(1).subtitle,
            "A larger block clock for public display on a tablet or other large screen.")
        compare(blockStatusSizePicker.itemAtIndex(0).rowIconSource.toString(),
            "image://images/blockclock-size-compact")
        compare(blockStatusSizePicker.itemAtIndex(1).rowIconSource.toString(),
            "image://images/blockclock-size-showcase")
        compare(blockStatusSizePicker.itemAtIndex(0).implicitHeight, 52)
        compare(blockStatusSizePicker.itemAtIndex(1).implicitHeight, 52)
        blockStatusSizePicker.activated(1 / 2)
        tryCompare(Theme, "blockclocksize", 1 / 2)
        compare(blockStatusSizePicker.currentText, "Showcase")

        compare(moneyFontPicker.subtitleRole, "description")
        compare(moneyFontPicker.minimumMenuWidth, 400)
        compare(moneyFontPicker.currentText, "Roboto Mono")
        tryVerify(function() { return moneyFontPicker.itemAtIndex(1) !== null })
        compare(moneyFontPicker.itemAtIndex(0).subtitle, "Included with Bitcoin Core")
        compare(moneyFontPicker.itemAtIndex(1).subtitle,
            "Uses your operating system’s default monospaced font")
        const embeddedMoneyFontWidth = moneyFontPicker.width
        moneyFontPicker.activated("best_system")
        compare(optionsModel.moneyFontChoice, "best_system")
        compare(moneyFontPicker.currentText, "System monospace")
        tryVerify(function() { return moneyFontPicker.width > embeddedMoneyFontWidth })
        moneyFontPicker.activated("embedded")
        compare(optionsModel.moneyFontChoice, "embedded")
        compare(moneyFontPicker.currentText, "Roboto Mono")
        tryCompare(moneyFontPicker, "width", embeddedMoneyFontWidth)

        const displayUnits = [
            { value: 1, text: "mBTC" },
            { value: 2, text: "bits" },
            { value: 3, text: "sat" },
            { value: 0, text: "BTC" }
        ]
        for (let index = 0; index < displayUnits.length; ++index) {
            const unit = displayUnits[index]
            displayUnitPicker.activated(unit.value)
            compare(optionsModel.displayUnit, unit.value)
            compare(displayUnitPicker.currentValue, unit.value)
            compare(displayUnitPicker.currentText, unit.text)
        }

        const sizeSlider = findChild(view, "displayTextSizeSlider")
        compare(sizeSlider.minimumValue, 0)
        compare(sizeSlider.maximumValue, Theme.textScales.length - 1)
        compare(sizeSlider.Accessible.description, findChild(view, "displayTextSizePercentage").text)

        const reduceMotionRow = findChild(view, "displayReduceMotionRow")
        verify(reduceMotionRow !== null)
        compare(reduceMotionRow.checked, Theme.reduceMotion)
        const reduceMotion = Theme.reduceMotion
        reduceMotionRow.switchItem.toggle()
        reduceMotionRow.switchItem.toggled()
        compare(Theme.reduceMotion, !reduceMotion)
        Theme.reduceMotion = reduceMotion

        // The View menu and its shortcut change the setting too, so the row follows it.
        const hideAmountsRow = findChild(view, "displayHideAmountsRow")
        verify(hideAmountsRow !== null)
        try {
            optionsModel.hideAmounts = false
            compare(hideAmountsRow.checked, false)
            hideAmountsRow.switchItem.toggle()
            hideAmountsRow.switchItem.toggled()
            compare(optionsModel.hideAmounts, true)
            optionsModel.hideAmounts = false
            compare(hideAmountsRow.checked, false)
            optionsModel.hideAmounts = true
            compare(hideAmountsRow.checked, true)
        } finally {
            optionsModel.hideAmounts = false
        }

        const textSizeSlider = findChild(view, "displayTextSizeSlider")
        verify(textSizeSlider !== null)
        const textScale = Theme.textScale
        try {
            Theme.textScale = 1
            compare(textSizeSlider.value, 3)
            compare(textSizeSlider.stepCount, 6)
            textSizeSlider.value = 5
            textSizeSlider.moved()
            compare(Theme.textScale, Theme.textScales[5])
            Theme.textScale = Theme.textScales[0]
            compare(textSizeSlider.value, 0)
        } finally {
            Theme.textScale = textScale
        }

        designSystemRow.clicked()
        tryCompare(view.pageContainer, "depth", 2)
        verify(findChild(view, "displayDesignSystemPage") !== null)
    }

    function test_mempoolPageUsesStandardFormRows() {
        const view = createTemporaryObject(settingsViewComponent, settingsWindow.contentItem)
        verify(view !== null); view.selectSection("mempool"); readyEditor(view)
        const transactions = findChild(view, "mempoolTransactionsRow")
        const memory = findChild(view, "mempoolMemoryUsedRow")
        const limit = findChild(view, "effectiveSetting_maxmempool")
        verify(transactions !== null); verify(memory !== null); verify(limit !== null)
        compare(transactions.titleTextStyle.font.pixelSize, Theme.text.description.font.pixelSize)
        compare(transactions.valueTextStyle.font.pixelSize, Theme.text.description.font.pixelSize)
        compare(memory.valueTextStyle.font.pixelSize, Theme.text.description.font.pixelSize)
        compare(limit.titleTextStyle.font.pixelSize, Theme.text.description.font.pixelSize)
        editSetting(view, "maxmempool", "512")
        compare(optionsModel.settingsWritesForTest(), 0)
        saveSettings(view)
        compare(view.navigation.editor.entry("maxmempool").nextStartText, "512")
        editSetting(view, "maxmempool", "0")
        tryCompare(view.navigation.editor, "canSave", true)
        compare(view.navigation.editor.entry("maxmempool").error, "")
        // Core's documented zero value means no memory limit.
        saveSettings(view); compare(view.navigation.editor.entry("maxmempool").nextStartText, "0")
        editSetting(view, "maxmempool", "0x")
        compare(view.navigation.editor.canSave, false)
        verify(view.navigation.editor.entry("maxmempool").error.length > 0)
        compare(optionsModel.settingsWritesForTest(), 2)
    }

    function test_mempoolPollingAndRestartNoticeFollowVisibility() {
        const view = createTemporaryObject(settingsViewComponent, settingsWindow.contentItem)
        verify(view !== null); compare(nodeModel.mempoolInfoPollingActive, false)
        view.selectSection("mempool"); readyEditor(view)
        const page = findChild(view, "mempoolSettingsPage")
        const notice = findChild(view, "settingsRestartNotice")
        verify(page !== null); verify(notice !== null)
        tryCompare(nodeModel, "mempoolInfoPollingActive", true); compare(notice.visible, false)
        editSetting(view, "maxmempool", "512"); saveSettings(view)
        tryCompare(notice, "visible", true)
        view.selectSection("display"); tryCompare(nodeModel, "mempoolInfoPollingActive", false)
        compare(findChild(view, "mempoolSettingsPage"), page)
        compare(notice.visible, true) // Required restart is global, not tab-local.
        view.selectSection("mempool"); tryCompare(nodeModel, "mempoolInfoPollingActive", true)
        view.visible = false; tryCompare(nodeModel, "mempoolInfoPollingActive", false)
        view.visible = true; tryCompare(nodeModel, "mempoolInfoPollingActive", true)
        view.destroy(); wait(0); compare(nodeModel.mempoolInfoPollingActive, false)
    }

    function test_doneAsksBeforeDiscardingAProxyDraft() {
        // Done leaves Settings; an unsaved proxy edit asks first and Keep
        // editing preserves it. (The onboarding proxy page's close is
        // covered by tst_onboarding_datadir's dirty proxy exit test.)
        const view = createTemporaryObject(settingsViewComponent, settingsWindow.contentItem)
        verify(view !== null); view.selectSection("connection"); readyEditor(view)
        let doneCalls = 0
        view.doneClicked.connect(() => { doneCalls += 1 })
        const input = findChild(view, "proxySettingInput")
        editSetting(view, "proxy", "10.0.0.1:9050")
        tryCompare(view.navigation.editor, "dirty", true)
        clickDoneControl(view, true)
        tryCompare(view.navigation.editor, "closeQuestion", true)
        compare(doneCalls, 0)
        compare(view.selectedSectionId, "connection")
        findChild(findChild(view, "settingsCloseQuestion").contentItem, "settingsCloseKeep").clicked(); tryCompare(view.navigation.editor, "closeQuestion", false)
        compare(doneCalls, 0)
        compare(input.text, "10.0.0.1:9050")
        compare(view.navigation.editor.entry("proxy").text, "10.0.0.1:9050")
        compare(optionsModel.settingsWritesForTest(), 0)
    }

    function test_connectionProxyPageUsesRedesignedFormAndDraftCommit() {
        const view = createTemporaryObject(settingsViewComponent, settingsWindow.contentItem)
        verify(view !== null); view.selectSection("connection"); readyEditor(view)
        const page = findChild(view, "connectionSettingsPage")
        const row = findChild(page, "effectiveSetting_proxy")
        const input = findChild(page, "proxySettingInput")
        const save = findChild(view, "settingsSaveBatchButton")
        verify(page !== null); verify(row !== null); verify(input !== null); verify(save !== null)
        compare(row.titleTextStyle.font.pixelSize, Theme.text.description.font.pixelSize)
        compare(save.enabled, false)
        editSetting(view, "proxy", "10.0.0.1:9050")
        tryCompare(view.navigation.editor, "canSave", true)
        compare(optionsModel.settingsWritesForTest(), 0)
        view.selectSection("display"); tryCompare(view.navigation.editor, "closeQuestion", true)
        compare(view.selectedSectionId, "connection")
        findChild(findChild(view, "settingsCloseQuestion").contentItem, "settingsCloseKeep").clicked(); tryCompare(view.navigation.editor, "closeQuestion", false)
        compare(input.text, "10.0.0.1:9050")
        saveSettings(view)
        compare(view.navigation.editor.entry("proxy").nextStartText, "10.0.0.1:9050")
        compare(optionsModel.settingsWritesForTest(), 1)
    }

    function test_settingsBatchActionsFollowEditableSectionsAndHighlightOnlySave() {
        const view = createTemporaryObject(settingsViewComponent, settingsWindow.contentItem, {wallet: testWalletModel})
        verify(view !== null); readyEditor(view)
        const bar = findChild(view, "settingsEditorBar")
        const actions = findChild(view, "settingsBatchActions")
        const save = findChild(view, "settingsSaveBatchButton")
        const discard = findChild(view, "settingsDiscardBatchButton")
        const refresh = findChild(view, "settingsRefreshBatchButton")
        const sidebarSurface = findChild(view, "settingsSidebarSurface")
        verify(bar !== null); verify(actions !== null); verify(save !== null); verify(discard !== null); verify(refresh !== null); verify(sidebarSurface !== null)

        // Wallet, Display and top-level About do not edit the settings batch
        // and keep the full detail height for the page container.
        for (const section of ["wallet", "display", "about"]) {
            view.selectSection(section)
            tryCompare(view, "selectedSectionId", section)
            compare(actions.visible, false)
            compare(bar.visible, false)
            tryCompare(view.pageContainer, "height", view.height)
        }

        // Editable batch sections show the bar under the form, not under the sidebar,
        // sized to its own content rather than splitting the detail height in half.
        for (const section of ["storage", "connection", "mempool", "external-signer"]) {
            view.selectSection(section)
            tryCompare(view, "selectedSectionId", section)
            compare(actions.visible, true)
            compare(bar.visible, true)
            tryVerify(function() { return save.mapToItem(view, 0, 0).x >= sidebarSurface.width })
            tryVerify(function() { return bar.height > 0 && bar.height < view.pageContainer.height / 4 })
        }

        // Save settings is the only orange-highlighted action.
        verify(Qt.colorEqual(save.backgroundColor, Theme.color.accent))
        verify(!Qt.colorEqual(discard.backgroundColor, Theme.color.accent))
        verify(!Qt.colorEqual(refresh.backgroundColor, Theme.color.accent))
        editSetting(view, "proxy", "10.0.0.1:9050")
        tryCompare(save, "enabled", true)
        discard.clicked()
        tryCompare(view.navigation.editor, "dirty", false)

        // Developer options under About edits the settings batch too.
        view.selectSection("about")
        tryCompare(view, "selectedSectionId", "about")
        compare(actions.visible, false)
        const devRow = findChild(view, "aboutDeveloperRow")
        verify(devRow !== null); devRow.clicked()
        tryCompare(view.pageContainer, "depth", 2)
        compare(actions.visible, true)
    }

    function test_settingsViewCanInstantiateEveryVisibleTopLevelDestination() {
        const view = createTemporaryObject(settingsViewComponent, settingsWindow.contentItem)
        verify(view !== null)

        const destinations = [
            { id: "wallet", objectName: "walletSettingsPage" },
            { id: "external-signer", objectName: "externalSignerSettingsPage" },
            { id: "display", objectName: "displaySettingsPage" },
            { id: "window-behavior", objectName: "windowBehaviorSettingsPage" },
            { id: "storage", objectName: "storageSettingsPage" },
            { id: "connection", objectName: "connectionSettingsPage" },
            { id: "network-traffic", objectName: "networkTrafficSettingsPage" },
            { id: "mempool", objectName: "mempoolSettingsPage" },
            { id: "rpc-console", objectName: "rpcConsoleSettingsPage" },
            { id: "debug-log", objectName: "debugLogView" },
            { id: "about", objectName: "aboutSettingsPage" }
        ]

        for (let index = 0; index < destinations.length; ++index) {
            const destination = destinations[index]
            view.selectSection(destination.id, true)
            compare(view.selectedSectionId, destination.id)
            tryCompare(view.pageContainer, "depth", 1)
            verify(findChild(view, destination.objectName) !== null,
                   "Expected instantiated destination " + destination.id)
            if (destination.id === "wallet") {
                const walletInfoSection = findChild(view, "walletInfoSection")
                const walletActionsSection = findChild(view, "walletActionsSection")
                verify(walletInfoSection !== null)
                verify(walletActionsSection !== null)
                compare(walletInfoSection.title, "Wallet info")
                compare(walletActionsSection.title, "Wallet actions")
            }
            if (destination.id === "external-signer") {
                const signerPage = findChild(view, "externalSignerSettingsPage")
                const introduction = findChild(view, "externalSignerIntroduction")
                const signerSection = findChild(view, "externalSignerPathSection")
                const signerPathRow = findChild(view, "externalSignerPathRow")
                const signerFooter = findChild(view, "externalSignerPathSectionFooter")
                const signerPathInput = findChild(view, "externalSignerPathInput")
                const signerPathFocusBorder = findChild(view, "externalSignerPathInputFocusRing")
                const signerStatusIndicator = findChild(view, "externalSignerStatusIndicator")
                const signerStatusText = findChild(view, "externalSignerStatusText")
                const checkDeviceButton = findChild(view, "externalSignerCheckDeviceButton")
                verify(signerPage !== null)
                verify(introduction !== null)
                verify(signerSection !== null)
                verify(signerPathRow !== null)
                verify(signerFooter !== null)
                verify(signerPathInput !== null)
                verify(signerPathFocusBorder !== null)
                verify(signerStatusIndicator !== null)
                verify(signerStatusText !== null)
                verify(checkDeviceButton !== null)
                compare(introduction.title, "")
                compare(introduction.description, "Connect a hardware wallet or another external signing tool.")
                compare(signerPage.maximumContentWidth, 840)
                compare(signerSection.title, "Signer path")
                compare(signerPathRow.topPadding, 16)
                compare(signerPathRow.bottomPadding, 16)
                compare(signerFooter.text,
                    "The add wallet flow can offer external wallets when exactly one supported signer is connected.")
                // Discovery reports asynchronously; the indicator stays neutral until then.
                finishSignerDiscovery()
                tryCompare(signerStatusIndicator, "color", signerPage.signerConnected ? Theme.color.success : Theme.color.danger)
                compare(checkDeviceButton.text, "Check device")
                compare(signerPathInput.height, Theme.size.control)
                compare(signerPathInput.Accessible.name, "Signer path")
                compare(signerPathInput.background.color, Theme.color.surface2)
                compare(signerPathInput.background.radius, Theme.radius.field)
                compare(signerPathFocusBorder.parent.ringColor, Theme.color.neutral6)
                compare(signerStatusIndicator.width, 10)
                compare(signerStatusIndicator.height, 10)
            }
            if (destination.id === "about") {
                const versionRow = findChild(view, "aboutVersionRow")
                const versionValue = findChild(view, "aboutVersionRowValue")
                verify(versionRow !== null)
                verify(versionValue !== null)
                compare(versionRow.value, BuildInfo.fullClientVersion)
                compare(versionValue.text, BuildInfo.fullClientVersion)
                verify(versionValue.text.length > 0)
                // Version opens an external URL, so it carries the external-link
                // icon like the other About links, not the in-app chevron.
                compare(versionRow.linkIconSource.toString(), "image://images/export")
                compare(versionRow.showsDisclosureIndicator, false)
            }
        }
    }
}

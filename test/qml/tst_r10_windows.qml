// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.2
import org.bitcoincore.qt 1.0
import "../../qml/controls"
TestCase {
    id: testCase
    name: "R10Windows"
    when: windowShown
    width: 900
    height: 740
    property var windows: []
    Component { id: finishedSpy; SignalSpy { signalName: "finished" } }
    function init() {
        optionsModel.resetSettingsEditorsForTest()
        testSetup.failOnWarnings()
        // Keep the engine singleton attached to its original namespace/store.
        verify(typeof Theme.dark === "boolean")
        testStartupDiagnostics.project("", [], false, false)
        optionsModel.previewError = ""
        optionsModel.setStorageStatusForTest(false, 1000, "", "")
        optionsModel.settingsReady = true
        optionsModel.settingsPending = false
    }
    function cleanup() {
        for (let i = 0; i < windows.length; ++i) {
            const receipt = testSetup.watchDestruction(windows[i])
            windows[i].close()
            windows[i].destroy()
            tryCompare(receipt, "pending", 0)
            testSetup.releaseDestructionWatch(receipt)
        }
        windows = []
        testSetup.processDeferredDeletes()
        verify(testSetup.endControlledPreferences())
        testStartupDiagnostics.project("", [], false, false)
        optionsModel.previewError = ""
        ApplicationModels.setPreInitForTest(false)
    }
    function windowFor(url) {
        // preinit.qml reads the onboarding model, which only exists before the node starts.
        if (url.endsWith("/preinit.qml")) ApplicationModels.setPreInitForTest(true)
        const component = Qt.createComponent(url)
        compare(component.status, Component.Ready, component.errorString())
        // The minimal platform does not choose geometry from minimumSize.
        // Supply real initial geometry before the native content is completed.
        const window = component.createObject(testCase, {width: 900, height: 740})
        verify(window !== null, component.errorString())
        windows.push(window)
        window.requestActivate()
        tryCompare(window, "active", true)
        tryCompare(window.contentItem, "width", 900)
        tryCompare(window.contentItem, "height", 740)
        return window
    }
    function textItem(root, text) {
        if (!root) return null
        if (root.text === text) return root
        const children = root.children || []
        for (let i = 0; i < children.length; ++i) { const found = textItem(children[i], text); if (found) return found }
        return null
    }
    function test_startup_error_uses_real_shaped_diagnostic_projection_data() {
        return [{tag: "no-detail-model", details: false}, {tag: "complete-details", details: true}]
    }
    function test_startup_error_uses_real_shaped_diagnostic_projection(data) {
        testStartupDiagnostics.project("Original startup failure", data.details ? ["Detail one", "Detail two"] : [], data.details, true)
        const view = windowFor("qrc:/qml/pages/initerrormessage.qml")
        compare(ApplicationModels.startupError, "Original startup failure")
        compare(ApplicationModels.startupErrorPending, true)
        verify(textItem(view.contentItem, "Original startup failure") !== null)
        compare(view.minimumHeight, 250)
        const button = findChild(view.contentItem, "startupDetailsButton")
        const details = findChild(view.contentItem, "startupCompleteDiagnostics")
        verify(button !== null && details !== null)
        compare(button.visible, data.details)
        compare(details.visible, false)
        testStartupDiagnostics.project("Final startup failure", data.details ? ["Detail one", "Detail two"] : [], data.details, false)
        compare(ApplicationModels.startupErrorPending, false)
        verify(textItem(view.contentItem, "Final startup failure") !== null)
        if (data.details) {
            // Assert the minimal backend's exact unsupported native-size hint.
            // QtTest fails this row if the expected warning is absent.
            if (Qt.platform.pluginName === "minimal")
                ignoreWarning("This plugin does not support propagateSizeHints()")
            mouseClick(button)
            compare(view.showCompleteDiagnostics, true)
            compare(view.minimumHeight, 520)
            compare(details.visible, true)
            tryCompare(details, "count", 2)
            compare(testStartupDiagnostics.contentAt(1), "Detail two")
            if (Qt.platform.pluginName === "minimal")
                ignoreWarning("This plugin does not support propagateSizeHints()")
            mouseClick(button)
            compare(view.showCompleteDiagnostics, false)
            compare(view.minimumHeight, 250)
        }
    }
    function test_preinit_holds_public_preferences_until_gui_publication() {
        const store = testSetup.beginControlledPreferences(testCase, false)
        verify(store !== null)
        const ticket = testSetup.controlledPreferenceOperationId()
        const view = windowFor("qrc:/qml/pages/preinit.qml")
        compare(store.pending, true)
        compare(store.ready, false)
        verify(testSetup.runControlledPreferenceWork())
        compare(store.ready, false)
        compare(testSetup.controlledPreferenceOperationId(), ticket)
        verify(testSetup.settleControlledPreferences())
        compare(store.ready, true)
        compare(store.error, "")
        compare(findChild(view, "preInitPreferencesError").visible, false)
    }
    function test_preinit_retry_owns_one_original_read() {
        const store = testSetup.beginControlledPreferences(testCase, true)
        verify(store !== null)
        const view = windowFor("qrc:/qml/pages/preinit.qml")
        verify(testSetup.settleControlledPreferences())
        compare(store.ready, false)
        verify(store.error.length > 0)
        const popup = findChild(view, "preInitPreferencesError")
        const retry = findChild(view, "preInitPreferencesRetry")
        verify(popup !== null && retry !== null)
        tryCompare(popup, "opened", true)
        const first = testSetup.controlledPreferenceOperationId()
        mouseClick(retry)
        compare(testSetup.controlledPreferenceOperationId(), first + 1)
        const retryTicket = testSetup.controlledPreferenceOperationId()
        mouseClick(retry)
        compare(testSetup.controlledPreferenceOperationId(), retryTicket)
        verify(testSetup.runControlledPreferenceWork())
        compare(store.ready, false)
        verify(testSetup.settleControlledPreferences())
        compare(store.ready, true)
        compare(store.error, "")
        tryCompare(popup, "visible", false)
    }
    function test_preinit_finish_guard_and_start_overlay_own_the_public_intent() {
        const view = windowFor("qrc:/qml/pages/preinit.qml")
        const navigation = findChild(view.contentItem, "onboardingWizardNavigationBar")
        verify(navigation !== null)
        const wizard = navigation.parent
        const spy = createTemporaryObject(finishedSpy, this, {target: view})
        optionsModel.previewError = "Original preview rejected"
        wizard.finished()
        compare(spy.count, 0)
        compare(view.starting, false)
        compare(findChild(view, "preInitPreviewErrorText").visible, true)
        optionsModel.previewError = ""
        wizard.finished()
        tryCompare(spy, "count", 1)
        compare(view.completed, true)
        compare(view.starting, true)
        compare(findChild(view, "preInitStartupOverlay").visible, true)
        compare(findChild(view, "preInitStartupBusyIndicator").running, true)
        optionsModel.previewError = "Late preview must not replace start"
        compare(findChild(view, "preInitPreviewErrorText").visible, false)
    }
}

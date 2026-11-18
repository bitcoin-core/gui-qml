// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
import QtQuick 2.15
import QtTest 1.2
import org.bitcoincore.qt 1.0
TestCase {
    id: testCase
    name: "OnboardingHandoff"
    when: windowShown
    width: 900
    height: 740
    property var window: null
    Component { id: finishedSpy; SignalSpy { signalName: "finished" } }
    function init() {
        optionsModel.resetSettingsEditorsForTest()
        optionsModel.previewError = ""
        optionsModel.setStorageStatusForTest(false, 1000, "", "")
        optionsModel.settingsReady = true
        optionsModel.settingsPending = false
        testSetup.failOnWarnings()
        // preinit.qml reads the onboarding model, which only exists before the node starts.
        ApplicationModels.setPreInitForTest(true)
        const preferences = testSetup.beginControlledPreferences(testCase, false)
        verify(preferences !== null)
        verify(testSetup.settleControlledPreferences())
        verify(preferences.ready)
        compare(preferences.error, "")
        const component = Qt.createComponent("qrc:/qml/pages/preinit.qml")
        verify(component.status === Component.Ready, component.errorString())
        window = component.createObject(testCase, {width: 900, height: 740})
        verify(window !== null, component.errorString())
        window.requestActivate()
        tryCompare(window, "active", true)
        tryCompare(window.contentItem, "width", window.width)
        verify(window.width >= window.minimumWidth)
    }
    function cleanup() {
        if (!window) {
            ApplicationModels.setPreInitForTest(false)
            return
        }
        const receipt = testSetup.watchDestruction(window)
        window.close()
        window.destroy()
        tryCompare(receipt, "pending", 0)
        testSetup.releaseDestructionWatch(receipt)
        window = null
        optionsModel.resetSettingsEditorsForTest()
        testSetup.processDeferredDeletes()
        verify(testSetup.endControlledPreferences())
        ApplicationModels.setPreInitForTest(false)
    }
    function test_error_projection_exposes_only_the_appropriate_recovery_data() {
        return [{tag: "pre-write-failure", phase: "failed", retry: true, query: false, complete: false},
                {tag: "known-partial", phase: "partial", retry: false, query: false, complete: true},
                {tag: "unknown", phase: "uncertain", retry: false, query: true, complete: false}]
    }
    function test_error_projection_exposes_only_the_appropriate_recovery(data) {
        optionsModel.projectSetupForTest(data.phase)
        compare(window.completed, false)
        compare(findChild(window.contentItem, "preInitSetupWizard").enabled, data.retry)
        compare(window.starting, false)
        compare(findChild(window.contentItem, "preInitRetrySetupChecks").visible, data.retry)
        compare(findChild(window.contentItem, "preInitQuerySetupOutcome").visible, data.query)
        compare(findChild(window.contentItem, "preInitCompleteSavedSetup").visible, data.complete)
        compare(findChild(window.contentItem, "preInitSettingsCommitError").text, optionsModel.setupCommitError)
    }
    function test_query_is_an_explicit_command_and_pending_disables_repeat() {
        optionsModel.projectSetupForTest("uncertain")
        const query = findChild(window.contentItem, "preInitQuerySetupOutcome")
        tryCompare(query, "visible", true)
        verify(query.enabled)
        tryVerify(function() { return query.width > 0 && query.height > 0 })
        // The recovery flags change a Row's participating children. Force
        // its presentation layout before mapping real window input.
        query.parent.forceLayout()
        compare(findChild(window, "preInitPreferencesError").visible, false)
        mousePress(query, query.width / 2, query.height / 2)
        verify(query.down)
        mouseRelease(query, query.width / 2, query.height / 2)
        compare(optionsModel.setupQueriesForTest(), 1)
        compare(optionsModel.setupCompletionsForTest(), 0)
        compare(query.enabled, false)
        compare(window.completed, false)
    }
    function test_partial_completion_does_not_reenter_wizard_finish() {
        optionsModel.projectSetupForTest("partial")
        const complete = findChild(window.contentItem, "preInitCompleteSavedSetup")
        tryCompare(complete, "visible", true)
        verify(complete.enabled)
        tryVerify(function() { return complete.width > 0 && complete.height > 0 })
        complete.parent.forceLayout()
        compare(findChild(window, "preInitPreferencesError").visible, false)
        mousePress(complete, complete.width / 2, complete.height / 2)
        verify(complete.down)
        mouseRelease(complete, complete.width / 2, complete.height / 2)
        compare(optionsModel.setupCompletionsForTest(), 1)
        compare(optionsModel.setupQueriesForTest(), 0)
        compare(complete.enabled, false)
        compare(window.completed, false)
    }
    function test_known_commit_projects_completion_and_notifies_once() {
        const spy = finishedSpy.createObject(testCase, {target: window})
        verify(spy.valid)
        optionsModel.projectSetupForTest("completed")
        compare(window.completed, true)
        compare(window.starting, true)
        compare(findChild(window.contentItem, "preInitStartupOverlay").visible, true)
        compare(spy.count, 1)
        spy.destroy()
    }
    function test_pending_or_unknown_is_not_completion_data() {
        return [{tag: "pending", phase: "pending"}, {tag: "unknown", phase: "uncertain"}]
    }
    function test_pending_or_unknown_is_not_completion(data) {
        optionsModel.projectSetupForTest(data.phase)
        compare(window.completed, false)
        compare(findChild(window.contentItem, "preInitStartupOverlay").visible, data.phase === "pending")
    }
}

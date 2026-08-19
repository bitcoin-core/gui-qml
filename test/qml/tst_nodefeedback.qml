// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Window 2.15
import QtTest 1.2
import "../../qml/components"
import "../../qml/controls"
import "../../qml/pages/node"

TestCase {
    name: "NodeFeedback"
    when: windowShown
    width: 640
    height: 520

    Window {
        id: testWindow
        width: 640
        height: 520
        visible: true
    }

    Component {
        id: actionsComponent
        NodeStatusActions {}
    }

    Component {
        id: nodeRunnerComponent
        NodeRunner {}
    }

    Component {
        id: warningsPopupComponent
        NodeWarningsPopup {}
    }

    Component {
        id: informationPopupComponent
        NodeInformationPopup {}
    }

    Component {
        id: runtimeDialogComponent
        NodeRuntimeDialog {}
    }

    Component {
        id: fatalPopupComponent
        NodeFatalErrorPopup {}
    }

    Component {
        id: alertPopupComponent
        AlertPopup {
            title: "Delete wallet?"
            message: "Are you sure?"
            messageObjectName: "alertPopupMessageForTest"

            AlertAction {
                text: "Cancel"
                role: AlertAction.Cancel
                buttonObjectName: "alertCancelButton"
            }

            AlertAction {
                text: "Delete"
                role: AlertAction.Destructive
                buttonObjectName: "alertDeleteButton"
                onTriggered: destructiveAlertTriggered = true
            }
        }
    }

    property bool destructiveAlertTriggered: false

    function init() {
        testWindow.width = 640
        testWindow.height = 520
        nodeModel.setWarningsForTest([])
        nodeModel.setStartupErrorForTest("")
        nodeModel.answerRuntimeDialog(DialogButtonBox.Cancel)
        destructiveAlertTriggered = false
    }

    function longWarningText() {
        return "This warning contains enough text to exceed the available popup width and must wrap onto multiple lines instead of being clipped by the containing field."
    }

    function verifyWraps(textItem) {
        compare(textItem.wrapMode, Text.WordWrap)
        for (let i = 0; i < 20; ++i) {
            if (textItem.width > 0 && textItem.lineCount > 1) {
                return
            }
            wait(25)
        }
        verify(textItem.width > 0)
        verify(textItem.lineCount > 1)
    }

    function waitForChild(parent, objectName) {
        for (let i = 0; i < 20; ++i) {
            const child = findChild(parent, objectName)
            if (child !== null) {
                return child
            }
            wait(25)
        }
        return null
    }

    function test_alert_popup_actions_and_text_styles() {
        const popup = createTemporaryObject(alertPopupComponent, testWindow.contentItem)
        verify(popup !== null)

        popup.open()
        tryCompare(popup, "opened", true)

        const title = findChild(popup, "alertTitle")
        verify(title !== null)
        compare(title.font.pixelSize, Theme.text.subtitle.pixelSize)
        compare(title.lineHeight, Theme.text.subtitle.lineHeight)

        const message = findChild(popup, "alertPopupMessageForTest")
        verify(message !== null)
        compare(message.font.pixelSize, Theme.text.description.pixelSize)
        compare(message.lineHeight, Theme.text.description.lineHeight)

        compare(popup.visibleActions.length, 2)
        compare(popup.visibleActions[1].buttonObjectName, "alertDeleteButton")
        const deleteButton = waitForChild(testWindow.contentItem, "alertDeleteButton")
        verify(deleteButton !== null)
        compare(deleteButton.backgroundColor, Theme.color.red)

        deleteButton.clicked()
        tryCompare(popup, "opened", false)
        verify(destructiveAlertTriggered)
    }

    function test_warning_action_visibility_follows_warning_state() {
        const actions = createTemporaryObject(actionsComponent, testWindow.contentItem)
        verify(actions !== null)
        wait(0)

        const warningButton = findChild(actions, "nodeWarningsButton")
        const infoButton = findChild(actions, "nodeInformationButton")
        verify(warningButton !== null)
        verify(infoButton !== null)
        compare(warningButton.visible, false)
        compare(infoButton.visible, true)

        nodeModel.setWarningsForTest(["Clock skew warning"])
        tryCompare(warningButton, "visible", true)
    }

    function test_node_runner_status_actions_align_with_settings_button() {
        nodeModel.setWarningsForTest(["Clock skew warning"])

        const runner = createTemporaryObject(nodeRunnerComponent, testWindow.contentItem)
        verify(runner !== null)
        runner.width = testWindow.width
        runner.height = testWindow.height
        wait(0)

        const warningButton = findChild(runner, "nodeWarningsButton")
        const infoButton = findChild(runner, "nodeInformationButton")
        const settingsButton = findChild(runner, "nodeSettingsButton")
        verify(warningButton !== null)
        verify(infoButton !== null)
        verify(settingsButton !== null)
        compare(findChild(runner, "consoleTabButton"), null)
        tryCompare(warningButton, "visible", true)

        compare(warningButton.height, 34)
        compare(infoButton.height, 34)
        compare(settingsButton.height, 34)

        const warningCenterY = warningButton.mapToItem(runner, 0, warningButton.height / 2).y
        const infoCenterY = infoButton.mapToItem(runner, 0, infoButton.height / 2).y
        const settingsCenterY = settingsButton.mapToItem(runner, 0, settingsButton.height / 2).y
        verify(Math.abs(warningCenterY - settingsCenterY) <= 0.5)
        verify(Math.abs(infoCenterY - settingsCenterY) <= 0.5)
    }

    function test_warning_popup_lists_current_warnings() {
        nodeModel.setWarningsForTest(["Warning one", "Warning two"])

        const popup = createTemporaryObject(warningsPopupComponent, testWindow.contentItem)
        verify(popup !== null)
        popup.open()
        tryCompare(popup, "opened", true)

        tryCompare(popup, "warningCount", 2)
        compare(popup.firstWarningText, "Warning one")
    }

    function test_warning_popup_wraps_long_warning_text() {
        nodeModel.setWarningsForTest([longWarningText()])

        const popup = createTemporaryObject(warningsPopupComponent, testWindow.contentItem)
        verify(popup !== null)
        popup.open()
        tryCompare(popup, "opened", true)

        tryCompare(popup, "warningCount", 1)
        tryCompare(popup, "firstWarningWrapMode", Text.WordWrap)
        for (let i = 0; i < 20; ++i) {
            if (popup.firstWarningLineCount > 1) {
                return
            }
            wait(25)
        }
        verify(popup.firstWarningLineCount > 1)
    }

    function test_feedback_popups_expand_in_wide_window() {
        testWindow.width = 900
        wait(0)

        const warningsPopup = createTemporaryObject(warningsPopupComponent, testWindow.contentItem)
        const informationPopup = createTemporaryObject(informationPopupComponent, testWindow.contentItem)
        const runtimePopup = createTemporaryObject(runtimeDialogComponent, testWindow.contentItem)
        const fatalPopup = createTemporaryObject(fatalPopupComponent, testWindow.contentItem)
        verify(warningsPopup !== null)
        verify(informationPopup !== null)
        verify(runtimePopup !== null)
        verify(fatalPopup !== null)

        compare(warningsPopup.contentMargin, 28)
        compare(informationPopup.contentMargin, 28)
        compare(runtimePopup.contentMargin, 28)
        compare(fatalPopup.contentMargin, 28)
        verify(warningsPopup.width > 460)
        verify(informationPopup.width > 520)
        verify(runtimePopup.width > 420)
        verify(fatalPopup.width > 420)
    }

    function test_node_information_popup_materializes_rows_on_open() {
        const popup = createTemporaryObject(informationPopupComponent, testWindow.contentItem)
        verify(popup !== null)
        popup.open()
        tryCompare(popup, "opened", true)

        tryCompare(popup, "informationRowCount", 3)
        compare(popup.firstInformationValue, "Bitcoin Core test")
    }

    function test_node_information_popup_wraps_long_warning_value() {
        nodeModel.setWarningsForTest([longWarningText()])

        const popup = createTemporaryObject(informationPopupComponent, testWindow.contentItem)
        verify(popup !== null)
        popup.open()
        tryCompare(popup, "opened", true)

        tryCompare(popup, "informationRowCount", 4)
        tryCompare(popup, "lastInformationValueWrapMode", Text.WordWrap)
        for (let i = 0; i < 20; ++i) {
            if (popup.lastInformationValueLineCount > 1) {
                return
            }
            wait(25)
        }
        verify(popup.lastInformationValueLineCount > 1)
    }

    function test_runtime_dialog_opens_from_node_model_and_answers() {
        const popup = createTemporaryObject(runtimeDialogComponent, testWindow.contentItem)
        verify(popup !== null)

        nodeModel.setRuntimeDialogForTest("Question", "Continue?", DialogButtonBox.Ok | DialogButtonBox.Abort, true)
        tryCompare(popup, "opened", true)
        wait(0)

        const title = findChild(popup, "nodeRuntimeDialogTitle")
        const message = findChild(popup, "nodeRuntimeDialogMessage")
        const ok = findChild(popup, "nodeRuntimeDialogButtonOk")
        const abort = findChild(popup, "nodeRuntimeDialogButtonAbort")
        verify(title !== null)
        verify(message !== null)
        verify(ok !== null)
        verify(abort !== null)
        compare(title.text, "Question")
        compare(message.text, "Continue?")
        compare(ok.visible, true)
        compare(abort.visible, true)

        mouseClick(ok, ok.width / 2, ok.height / 2)
        tryCompare(popup, "opened", false)
        compare(nodeModel.runtimeDialogVisible, false)
    }

    function test_runtime_dialog_exposes_all_core_buttons() {
        const popup = createTemporaryObject(runtimeDialogComponent, testWindow.contentItem)
        verify(popup !== null)

        const allButtons = DialogButtonBox.Ok
                         | DialogButtonBox.Yes
                         | DialogButtonBox.No
                         | DialogButtonBox.Abort
                         | DialogButtonBox.Retry
                         | DialogButtonBox.Ignore
                         | DialogButtonBox.Close
                         | DialogButtonBox.Cancel
                         | DialogButtonBox.Discard
                         | DialogButtonBox.Help
                         | DialogButtonBox.Apply
                         | DialogButtonBox.Reset
        nodeModel.setRuntimeDialogForTest("Full button set", "All Core buttons should be represented.", allButtons, true)
        tryCompare(popup, "opened", true)
        wait(0)

        const names = ["Ok", "Yes", "No", "Abort", "Retry", "Ignore", "Close", "Cancel", "Discard", "Help", "Apply", "Reset"]
        for (let i = 0; i < names.length; ++i) {
            const button = findChild(popup, "nodeRuntimeDialogButton" + names[i])
            verify(button !== null, "missing button " + names[i])
            verify(button.visible, "button not visible " + names[i])
        }
    }

    function test_runtime_dialog_wraps_long_message() {
        const popup = createTemporaryObject(runtimeDialogComponent, testWindow.contentItem)
        verify(popup !== null)

        nodeModel.setRuntimeDialogForTest("Warning", longWarningText(), DialogButtonBox.Ok, false)
        tryCompare(popup, "opened", true)

        const message = findChild(popup, "nodeRuntimeDialogMessage")
        verify(message !== null)
        verifyWraps(message)
    }

    function test_fatal_popup_uses_startup_error_and_shutdown_action() {
        const popup = createTemporaryObject(fatalPopupComponent, testWindow.contentItem)
        verify(popup !== null)

        nodeModel.setStartupErrorForTest("Fatal init failure")
        tryCompare(popup, "opened", true)

        const text = findChild(popup, "nodeFatalErrorText")
        verify(text !== null)
        compare(text.text, "Fatal init failure")
    }

    function test_fatal_popup_wraps_long_startup_error() {
        const popup = createTemporaryObject(fatalPopupComponent, testWindow.contentItem)
        verify(popup !== null)

        nodeModel.setStartupErrorForTest(longWarningText())
        tryCompare(popup, "opened", true)

        const text = findChild(popup, "nodeFatalErrorText")
        verify(text !== null)
        verifyWraps(text)
    }
}

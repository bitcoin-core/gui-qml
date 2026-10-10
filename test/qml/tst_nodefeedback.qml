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
        id: bannerComponent
        NodeBanner { width: 640 }
    }

    Component {
        id: nodeRunnerComponent
        NodeRunner {}
    }

    Component {
        id: nodeRunnerStackComponent
        PageStack {
            width: 640
            height: 520
            initialItem: NodeRunner {}
        }
    }

    Component {
        id: emptyPageComponent
        Page { background: null }
    }

    Component {
        id: detailsPopupComponent
        NodeDetailsPopup {}
    }

    Component {
        id: warningsPopupComponent
        NodeWarningsPopup {}
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

    SignalSpy {
        id: shutdownSpy
        target: nodeModel
        signalName: "requestedShutdown"
    }

    function init() {
        testWindow.width = 640
        testWindow.height = 520
        nodeModel.setWarningsForTest([])
        nodeModel.restoreDismissedWarnings()
        nodeModel.setStartupErrorForTest("")
        shutdownSpy.clear()
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

        const surface = findChild(popup, "alertPopupSurface")
        verify(surface !== null)
        compare(surface.color, Theme.color.neutral1)
        compare(surface.border.color, Theme.color.neutral2)
        compare(surface.radius, 10)
        compare(popup.dim, true)
        verify(popup.enter !== null)
        verify(popup.exit !== null)

        compare(popup.visibleActions.length, 2)
        compare(popup.visibleActions[1].buttonObjectName, "alertDeleteButton")
        const deleteButton = waitForChild(testWindow.contentItem, "alertDeleteButton")
        verify(deleteButton !== null)
        compare(deleteButton.backgroundColor, Theme.color.red)

        deleteButton.clicked()
        tryCompare(popup, "opened", false)
        verify(destructiveAlertTriggered)
    }

    function test_banner_follows_warnings_and_can_be_closed() {
        const banner = createTemporaryObject(bannerComponent, testWindow.contentItem)
        verify(banner !== null)
        compare(banner.shown, false)
        compare(banner.height, 0)

        nodeModel.setWarningsForTest(["Clock skew warning"])
        tryCompare(banner, "shown", true)
        verify(banner.height > 0)
        const text = findChild(banner, "nodeBannerText")
        verify(text.text.indexOf("<b>Warning:</b>") === 0)
        verify(text.text.indexOf("Clock skew warning") > 0)

        // Closing a warning dismisses it; a new warning brings the banner back
        // with only the new one.
        findChild(banner, "nodeBannerCloseButton").clicked()
        compare(banner.shown, false)
        compare(nodeModel.dismissedWarningList, ["Clock skew warning"])
        nodeModel.setWarningsForTest(["Clock skew warning", "Unknown new rules activated"])
        tryCompare(banner, "shown", true)
        verify(text.text.indexOf("Unknown new rules activated") > 0)
        verify(text.text.indexOf("Clock skew warning") < 0)
    }

    function test_banner_shows_errors_before_warnings() {
        nodeModel.setWarningsForTest(["Clock skew warning"])
        nodeModel.setStartupErrorForTest("Disk space is too low.")
        nodeModel.faulted = true
        const banner = createTemporaryObject(bannerComponent, testWindow.contentItem)
        tryCompare(banner, "shown", true)
        const text = findChild(banner, "nodeBannerText")
        compare(text.text, "<b>Error:</b> Disk space is too low.")
        compare(banner.status.bannerColor, Theme.color.red)
        nodeModel.faulted = false
        tryCompare(text, "text", "<b>Warning:</b> Clock skew warning")
    }

    function test_banner_escapes_and_wraps_long_warning_text() {
        nodeModel.setWarningsForTest(["<b>not markup</b> " + longWarningText()])
        const banner = createTemporaryObject(bannerComponent, testWindow.contentItem)
        tryCompare(banner, "shown", true)
        const text = findChild(banner, "nodeBannerText")
        verify(text.text.indexOf("&lt;b&gt;not markup&lt;/b&gt;") > 0)
        for (let i = 0; i < 20 && text.lineCount <= 1; ++i) wait(25)
        verify(text.lineCount > 1)
    }

    function test_node_runner_status_bar_aligns_with_settings_button() {
        nodeModel.setWarningsForTest(["Clock skew warning"])

        const runner = createTemporaryObject(nodeRunnerComponent, testWindow.contentItem)
        verify(runner !== null)
        runner.width = testWindow.width
        runner.height = testWindow.height
        wait(0)

        compare(findChild(runner, "nodeWarningsButton"), null)
        compare(findChild(runner, "nodeInformationButton"), null)
        const primary = findChild(runner, "nodeStatusPrimaryAction")
        const settingsButton = findChild(runner, "nodeSettingsButton")
        verify(primary !== null)
        verify(settingsButton !== null)
        compare(findChild(runner, "consoleTabButton"), null)
        tryCompare(findChild(runner, "nodeBanner"), "shown", true)

        const primaryCenterY = primary.mapToItem(runner, 0, primary.height / 2).y
        const settingsCenterY = settingsButton.mapToItem(runner, 0, settingsButton.height / 2).y
        verify(Math.abs(primaryCenterY - settingsCenterY) <= 0.5)
    }

    function test_node_runner_clock_follows_stack_visibility() {
        const stack = createTemporaryObject(nodeRunnerStackComponent, testWindow.contentItem)
        verify(stack !== null)
        const runner = findChild(stack, "nodeRunner")
        const clock = findChild(stack, "blockClock")
        verify(runner !== null)
        verify(clock !== null)
        tryCompare(clock, "renderingActive", true)

        stack.push(emptyPageComponent)
        tryCompare(stack, "depth", 2)
        tryCompare(runner, "visible", false, 1000)
        tryCompare(clock, "renderingActive", false)

        stack.pop()
        tryCompare(stack, "depth", 1)
        tryCompare(runner, "visible", true, 1000)
        tryCompare(clock, "renderingActive", true)
    }

    function test_feedback_popups_expand_in_wide_window() {
        testWindow.width = 900
        wait(0)

        const runtimePopup = createTemporaryObject(runtimeDialogComponent, testWindow.contentItem)
        const fatalPopup = createTemporaryObject(fatalPopupComponent, testWindow.contentItem)
        verify(runtimePopup !== null)
        verify(fatalPopup !== null)

        compare(runtimePopup.contentMargin, 28)
        compare(fatalPopup.contentMargin, 28)
        verify(runtimePopup.width > 420)
        verify(fatalPopup.width > 420)
    }

    function test_node_details_reads_about_rows_only_when_expanded() {
        const popup = createTemporaryObject(detailsPopupComponent, testWindow.contentItem)
        verify(popup !== null)
        popup.open()
        tryCompare(popup, "opened", true)
        compare(popup.aboutRows.length, 0)

        popup.moreExpanded = true
        // Rows covered elsewhere (network, peers, warnings) are not repeated.
        compare(popup.aboutRows.map(function(row) { return row.key + "=" + row.value }),
                ["Version=Bitcoin Core test", "Data directory=~/.bitcoin/regtest"])
        tryVerify(function() { return findChild(popup.contentItem, "nodeDetailsAboutValue_0") !== null })
        compare(findChild(popup.contentItem, "nodeDetailsAboutValue_0").text, "Bitcoin Core test")

        // Directories are links that open the folder; plain values are not.
        const dataDir = findChild(popup.contentItem, "nodeDetailsAboutValue_1")
        compare(dataDir.text, "~/.bitcoin/regtest")
        compare(dataDir.color, Theme.color.orange)
        compare(popup.aboutRows[1].url.toString(), "file:///home/test/.bitcoin/regtest")
        compare(findChild(popup.contentItem, "nodeDetailsAboutValue_1_link").enabled, true)
        compare(findChild(popup.contentItem, "nodeDetailsAboutValue_0_link").enabled, false)
        compare(popup.aboutRows[1].path, "/home/test/.bitcoin/regtest")

        // Hovering the link dims it; its colour already marks it as a link.
        const link = findChild(popup.contentItem, "nodeDetailsAboutValue_1_link")
        mouseMove(link, link.width / 2, link.height / 2)
        tryCompare(link, "containsMouse", true)
        compare(dataDir.opacity, 0.85)
        mouseMove(findChild(popup.contentItem, "nodeDetailsAboutValue_0"), 2, 2)
        tryCompare(link, "containsMouse", false)
        compare(dataDir.opacity, 1)

        nodeModel.setWarningsForTest([longWarningText()])
        popup.moreExpanded = false
        popup.moreExpanded = true
        compare(popup.aboutRows.length, 2)
        popup.close()
        tryCompare(popup, "visible", false)
        compare(popup.aboutRows.length, 0)
    }

    function test_warnings_popup_lists_each_warning_in_amber() {
        nodeModel.setWarningsForTest(["This is a pre-release test build", longWarningText()])
        const popup = createTemporaryObject(warningsPopupComponent, testWindow.contentItem)
        verify(popup !== null)
        popup.open()
        tryCompare(popup, "opened", true)

        compare(popup.warnings.length, 2)
        const first = findChild(popup.contentItem, "nodeWarningText_0")
        const second = findChild(popup.contentItem, "nodeWarningText_1")
        verify(first !== null && second !== null)
        compare(first.text, "This is a pre-release test build")
        // Each warning is its own amber card, and long text wraps.
        compare(first.parent.parent.border.color, Theme.color.amber)
        for (let i = 0; i < 20 && second.lineCount <= 1; ++i) wait(25)
        verify(second.lineCount > 1)

        // The list follows Core: a cleared warning leaves it.
        nodeModel.setWarningsForTest(["This is a pre-release test build"])
        compare(popup.warnings.length, 1)
        popup.close()
    }

    function test_warnings_can_be_dismissed_and_restored() {
        nodeModel.setWarningsForTest(["First warning", "Second warning", "Third warning"])
        const popup = createTemporaryObject(warningsPopupComponent, testWindow.contentItem)
        popup.open()
        tryCompare(popup, "opened", true)
        compare(popup.warnings.length, 3)

        // One at a time.
        findChild(popup.contentItem, "nodeWarningDismiss_1").clicked()
        compare(popup.warnings, ["First warning", "Third warning"])
        compare(popup.dismissed, ["Second warning"])
        // The count everywhere follows the active warnings.
        compare(popup.status.warningsText, "2 warnings")

        // Dismissed ones are a click away, muted, and can be restored.
        const show = findChild(popup.contentItem, "nodeWarningsShowDismissed")
        compare(show.text, "1 dismissed · Show")
        show.clicked()
        compare(show.text, "1 dismissed · Hide")
        tryVerify(function() { return findChild(popup.contentItem, "nodeDismissedWarningText_0") !== null })
        compare(findChild(popup.contentItem, "nodeDismissedWarningText_0").text, "Second warning")

        // All at once.
        findChild(popup.contentItem, "nodeWarningsDismissAll").clicked()
        compare(popup.warnings.length, 0)
        compare(popup.status.warningsText, "")
        compare(findChild(popup.contentItem, "nodeWarningsEmpty").text, "No new warnings.")

        findChild(popup.contentItem, "nodeWarningsRestoreAll").clicked()
        compare(popup.warnings.length, 3)
        compare(popup.dismissed.length, 0)
        popup.close()
        tryCompare(popup, "visible", false)
        compare(popup.showDismissed, false)
    }

    function test_warnings_popup_says_when_there_are_none() {
        const popup = createTemporaryObject(warningsPopupComponent, testWindow.contentItem)
        popup.open()
        tryCompare(popup, "opened", true)
        compare(popup.warnings.length, 0)
        compare(findChild(popup.contentItem, "nodeWarningText_0"), null)
        popup.close()
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
        const button = findChild(popup, "nodeFatalShutdownButton")
        verify(button !== null)
        mouseClick(button)
        compare(shutdownSpy.count, 1)
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

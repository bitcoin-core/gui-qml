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
        id: notificationsPopupComponent
        NodeNotificationsPopup {
            notifications: nodeModel.warningList.map(function(text) {
                return {text: text, icon: "alert-filled", color: Theme.color.amber}
            })
        }
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

    function test_node_runner_network_chip_is_read_only() {
        nodeModel.setWarningsForTest(["Clock skew warning"])

        const runner = createTemporaryObject(nodeRunnerComponent, testWindow.contentItem)
        verify(runner !== null)
        runner.width = testWindow.width
        runner.height = testWindow.height
        wait(0)

        const warningButton = findChild(runner, "nodeWarningsButton")
        const infoButton = findChild(runner, "nodeInformationButton")
        const networkIndicator = findChild(runner, "nodeRunnerNetworkIndicator")
        const settingsButton = findChild(runner, "nodeSettingsButton")
        compare(warningButton, null)
        compare(infoButton, null)
        verify(networkIndicator !== null)
        verify(settingsButton !== null)
        compare(findChild(runner, "consoleTabButton"), null)

        compare(networkIndicator.enabled, false)
        compare(networkIndicator.focusPolicy, Qt.NoFocus)
        networkIndicator.clicked()
        const informationPopup = waitForChild(testWindow.contentItem, "nodeOverviewInformationPopup")
        verify(informationPopup !== null)
        compare(informationPopup.opened, false)
    }

    function test_overview_summaries_and_notification_modals() {
        testWindow.width = 1200
        testWindow.height = 800
        const runner = createTemporaryObject(nodeRunnerComponent, testWindow.contentItem, { width: 1200, height: 800 })
        verify(runner !== null)
        const overview = findChild(runner, "nodeOverview")
        verify(overview !== null)
        compare(overview.compact, false)
        compare(overview.overviewContentWidth, 1100)
        compare(overview.informationRows.length, 4)
        compare(overview.informationRows[2].id, "startup-time")
        compare(findChild(overview, "nodeOverviewValue_startup-time").text, "Wed Sep 30 13:00:00 2026")
        compare(findChild(overview, "nodeOverviewValue_block-height"), null)
        compare(findChild(overview, "nodeOverviewTitle").text, "Node")
        compare(findChild(overview, "nodeOverviewTitle").font.pixelSize, Theme.text.headline.pixelSize)
        const pause = findChild(overview, "nodePauseButton")
        pause.clicked()
        compare(nodeModel.pause, true)
        compare(pause.text, "Resume node")
        pause.clicked()
        compare(nodeModel.pause, false)

        nodeModel.setWarningsForTest([longWarningText(), "Second warning"])
        const top = findChild(overview, "nodeTopNotification")
        tryCompare(findChild(overview, "nodeTopNotificationBanner"), "opacity", 1)
        tryCompare(top, "text", longWarningText())
        verifyWraps(top)
        mouseClick(findChild(overview, "nodeNotificationsSectionTitle"))
        const popup = findChild(testWindow.contentItem, "nodeNotificationsPopup")
        verify(popup !== null)
        tryCompare(popup, "opened", true)
        compare(popup.notificationCount, 2)
        const second = waitForChild(testWindow.contentItem, "nodeNotificationText_1")
        verify(second !== null)
        compare(second.text, "Second warning")
        nodeModel.setWarningsForTest(["Updated warning"])
        tryCompare(popup, "notificationCount", 1)
        tryCompare(top, "text", "Updated warning")
        findChild(popup, "nodeNotificationsCloseButton").clicked()
        tryCompare(popup, "visible", false)

        mouseClick(findChild(overview, "nodeInformationSectionTitle"))
        const information = findChild(testWindow.contentItem, "nodeOverviewInformationPopup")
        tryCompare(information, "opened", true)
        information.close()
        tryCompare(information, "visible", false)

        runner.width = 640
        tryCompare(overview, "compact", true)
        verify(waitForItemPolished(overview))
        const clock = findChild(overview, "nodeOverviewClockPanel")
        const sections = findChild(overview, "nodeOverviewSections")
        tryVerify(function() { return sections.y >= clock.y + clock.height })
        nodeModel.setWarningsForTest([])
        tryCompare(top, "text", "No current notifications.")
        nodeModel.numPeers = 1
        nodeModel.verificationProgress = 0.9999
        nodeModel.initialSyncComplete = false
        tryCompare(top, "text", "No current notifications.")
        nodeModel.initialSyncComplete = true
        tryCompare(top, "text", "Your node is up to date. All blocks verified.")
        nodeModel.verificationProgress = 0.75
        tryCompare(top, "text", "Your node is up to date. All blocks verified.")
        nodeModel.initialSyncComplete = false
        tryCompare(top, "text", "No current notifications.")
        nodeModel.numPeers = 0
    }

    function test_overview_pushes_peers_and_stops_refresh_when_hidden() {
        testWindow.width = 1200
        testWindow.height = 800
        const runner = createTemporaryObject(nodeRunnerComponent, testWindow.contentItem, { width: 1200, height: 800 })
        const overview = findChild(runner, "nodeOverview")
        compare(findChild(runner, "peersTabButton"), null)
        const stack = findChild(runner, "nodeNavigationStack")
        compare(stack.depth, 1)
        verify(waitForItemPolished(overview))
        for (const name of ["nodePeersSectionTitle", "nodeConnectedPeers", "nodeOutboundPeers", "nodeInboundPeers"]) {
            mouseClick(findChild(overview, name))
            tryCompare(stack, "depth", 2)
            tryCompare(stack, "busy", false)
            compare(stack.currentItem.objectName, "peers")
            compare(overview.visible, false)
            compare(peerTableModel.autoRefreshActive, true)
            const back = findChild(stack.currentItem, "peersNodeBackButton")
            compare(back.text, "Node")
            compare(findChild(stack.currentItem, "peersCloseButton"), null)
            mouseClick(back)
            tryCompare(stack, "depth", 1)
            tryCompare(stack, "busy", false)
            compare(stack.currentItem, overview)
            compare(peerTableModel.autoRefreshActive, false)
        }
        const peersSection = findChild(overview, "nodePeersSection")
        testWindow.requestActivate()
        tryCompare(testWindow, "active", true)
        peersSection.forceActiveFocus(Qt.TabFocusReason)
        tryCompare(peersSection, "activeFocus", true)
        keyClick(Qt.Key_Space)
        tryCompare(stack, "depth", 2)
        tryCompare(stack, "busy", false)
        const peers = stack.currentItem
        compare(peers.width, stack.width)
        compare(peers.height, stack.height)
        compare(peers.header, null)
        const split = findChild(peers, "peersNavigationSplitView")
        const separator = findChild(split, "navigationSplitSeparator")
        compare(split.y, 0)
        compare(split.height, peers.height)
        compare(separator.height, peers.height)
        compare(separator.mapToItem(peers, 0, 0).y, 0)
        const nodeBack = findChild(peers, "peersNodeBackButton")
        verify(nodeBack.mapToItem(peers, nodeBack.width, 0).x < separator.x)
        compare(peerTableModel.autoRefreshActive, true)
        runner.openPeers()
        compare(stack.depth, 2)
        compare(stack.currentItem, peers)
        runner.width = 390
        tryCompare(split, "isCompact", true)
        verify(nodeBack.visible)
        mouseClick(findChild(peers, "peersNodeBackButton"))
        tryCompare(stack, "depth", 1)
        tryCompare(stack, "busy", false)
        compare(peerTableModel.autoRefreshActive, false)
        runner.width = 1200
        verify(waitForItemPolished(overview))
        mouseClick(peersSection, peersSection.width - 2, peersSection.height - 2)
        tryCompare(stack, "depth", 2)
        tryCompare(stack, "busy", false)
        const retainedPeers = stack.currentItem
        findChild(runner, "widgetsTabButton").checked = true
        tryCompare(retainedPeers, "visible", false)
        compare(peerTableModel.autoRefreshActive, false)
        findChild(runner, "blockClockTabButton").checked = true
        tryCompare(retainedPeers, "visible", true)
        compare(peerTableModel.autoRefreshActive, true)
        runner.openNode()
        compare(stack.depth, 1)
        compare(stack.currentItem, overview)
        compare(peerTableModel.autoRefreshActive, false)
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

    function test_notifications_popup_lists_current_warnings() {
        nodeModel.setWarningsForTest(["Warning one", "Warning two"])

        const popup = createTemporaryObject(notificationsPopupComponent, testWindow.contentItem)
        verify(popup !== null)
        popup.open()
        tryCompare(popup, "opened", true)

        tryCompare(popup, "notificationCount", 2)
        compare(waitForChild(testWindow.contentItem, "nodeNotificationText_0").text, "Warning one")
    }

    function test_notifications_popup_wraps_long_warning_text() {
        nodeModel.setWarningsForTest([longWarningText()])

        const popup = createTemporaryObject(notificationsPopupComponent, testWindow.contentItem)
        verify(popup !== null)
        popup.open()
        tryCompare(popup, "opened", true)

        tryCompare(popup, "notificationCount", 1)
        const text = waitForChild(testWindow.contentItem, "nodeNotificationText_0")
        compare(text.wrapMode, Text.WordWrap)
        tryVerify(function() { return text.lineCount > 1 })
    }

    function test_feedback_popups_expand_in_wide_window() {
        testWindow.width = 900
        wait(0)

        const notificationsPopup = createTemporaryObject(notificationsPopupComponent, testWindow.contentItem)
        const informationPopup = createTemporaryObject(informationPopupComponent, testWindow.contentItem)
        const runtimePopup = createTemporaryObject(runtimeDialogComponent, testWindow.contentItem)
        const fatalPopup = createTemporaryObject(fatalPopupComponent, testWindow.contentItem)
        verify(notificationsPopup !== null)
        verify(informationPopup !== null)
        verify(runtimePopup !== null)
        verify(fatalPopup !== null)

        compare(notificationsPopup.contentMargin, 24)
        compare(informationPopup.contentMargin, 24)
        compare(runtimePopup.contentMargin, 28)
        compare(fatalPopup.contentMargin, 28)
        verify(notificationsPopup.width > 460)
        verify(informationPopup.width > 520)
        verify(runtimePopup.width > 420)
        verify(fatalPopup.width > 420)
    }

    function test_node_information_popup_materializes_rows_on_open() {
        const popup = createTemporaryObject(informationPopupComponent, testWindow.contentItem)
        verify(popup !== null)
        popup.open()
        tryCompare(popup, "opened", true)

        tryCompare(popup, "informationRowCount", 6)
        compare(popup.firstInformationValue, "Bitcoin Core test")

        const surface = findChild(popup, "nodeInformationSurface")
        const table = findChild(popup, "nodeInformationTable")
        const closeButton = findChild(popup, "nodeInformationCloseButton")
        verify(surface !== null)
        verify(table !== null)
        verify(closeButton !== null)
        compare(surface.color, Theme.color.neutral1)
        compare(surface.border.width, 0)
        compare(table.color, Theme.color.neutral2)
        compare(table.border.width, 0)
        verify(popup.enter !== null)
        verify(popup.exit !== null)
    }

    function test_node_information_popup_shows_wrapped_warning_above_table() {
        nodeModel.setWarningsForTest([longWarningText()])

        const popup = createTemporaryObject(informationPopupComponent, testWindow.contentItem)
        verify(popup !== null)
        popup.open()
        tryCompare(popup, "opened", true)

        tryCompare(popup, "informationRowCount", 6)
        const warningBanner = findChild(popup, "nodeInformationWarningBanner")
        const warningText = findChild(popup, "nodeInformationWarningText")
        verify(warningBanner !== null)
        verify(warningText !== null)
        compare(warningBanner.visible, true)
        compare(warningText.text, longWarningText())
        verifyWraps(warningText)
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

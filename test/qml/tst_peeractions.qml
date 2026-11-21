// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.2
import org.bitcoincore.qt 1.0
import "../../qml/controls"
import "../../qml/pages/node"

TestCase {
    name: "PeerActions"
    when: windowShown
    width: 520
    height: 720

    Window {
        id: testWindow
        width: 520
        height: 720
        visible: true
    }

    Component {
        id: peerDetailsComponent

        PeerDetails {
            width: 460
            height: 680
            details: testPeerDetailsModel
        }
    }

    Component {
        id: peersComponent

        Peers {
            width: 460
            height: 680
        }
    }

    Component {
        id: peersViewComponent

        PeersView {
            width: 900
            height: 680
        }
    }

    property string motionMode: ""
    function cleanup() {
        Theme.motion.mode = motionMode
    }
    function init() {
        testSetup.failOnWarnings()
        motionMode = Theme.motion.mode
        testWindow.requestActivate()
        tryCompare(testWindow, "active", true)
        testPeerDetailsModel.type = "Outbound Full Relay"
        testPeerDetailsModel.bytesSent = "1.0 MiB"
        nodeModel.resetPeerActionTestState()
        networkStatusModel.setNetworkOfflineForTest(false)
        peerTableModel.resetTestState()
        banListModel.resetTestState()
        peerListModelProxy.resetPeerInventoryForTest()
        testPeerDetailsModel.resetConnectionForTest()
        otherPeerDetailsModel.resetConnectionForTest()
    }

    function waitForPreferences(page) {
        const preferences = findChild(page, "peerFilterSettings")
        verify(preferences !== null)
        tryCompare(preferences, "ready", true)
        tryCompare(preferences, "pending", false)
        wait(0) // Deliver the store's bounded view notification turn.
    }

    function createPeerDetailsPage() {
        const page = createTemporaryObject(peerDetailsComponent, testWindow.contentItem)
        verify(page !== null)
        wait(0)
        return page
    }

    function createPeersPage() {
        const page = createTemporaryObject(peersComponent, testWindow.contentItem)
        verify(page !== null)
        wait(0)
        return page
    }

    function createPeersViewPage() {
        const page = createTemporaryObject(peersViewComponent, testWindow.contentItem)
        verify(page !== null)
        wait(0)
        return page
    }

    function waitForChild(parent, objectName) {
        for (let i = 0; i < 20; ++i) {
            const child = findChild(parent, objectName)
            if (child !== null) return child
            wait(25)
        }
        return null
    }

    function openBanConfirmation(page, durationObjectName) {
        const duration = findChild(page, durationObjectName)
        const confirmation = findChild(page, "banConfirmationPopup")
        verify(duration !== null)
        verify(confirmation !== null)

        duration.clicked()
        tryCompare(confirmation, "opened", true)
        verify(waitForChild(testWindow.contentItem, "banConfirmButton") !== null)
        return confirmation
    }

    function verifyPeerActionError(page, expectedText) {
        const popup = findChild(page, "peerActionErrorPopup")
        verify(popup !== null)
        tryCompare(popup, "opened", true)
        // Let the alert lay out its message before its buttons are clicked.
        verify(waitForItemPolished(popup.contentItem))
        const message = findChild(popup, "actionErrorMessage")
        verify(message !== null)
        verify(message.text.indexOf(expectedText) >= 0)
    }

    function verifyUnbanActionError(page) {
        const popup = findChild(page, "unbanActionErrorPopup")
        verify(popup !== null)
        tryCompare(popup, "opened", true)
        // Let the alert lay out its message before its buttons are clicked.
        verify(waitForItemPolished(popup.contentItem))
        const message = findChild(popup, "actionErrorMessage")
        verify(message !== null)
        verify(message.text.indexOf("Could not unban peer.") >= 0)
    }

    function createPeersPageWithBannedPopup() {
        const page = createPeersPage()
        const openButton = findChild(page, "viewBannedPeersButton")
        const popup = findChild(page, "bannedPeersPopup")
        verify(openButton !== null)
        verify(popup !== null)
        openButton.clicked()
        tryCompare(popup, "opened", true)
        return page
    }

    function requestConfirmation(page, list, ban, target) {
        if (list) {
            page.contextPeerDetails = target
            if (ban) page.requestContextPeerBan(PeerCommandOperation.Day, "1 day")
            else page.requestContextPeerDisconnect()
        } else {
            page.details = target
            if (ban) page.requestBan(PeerCommandOperation.Day, "1 day")
            else page.requestDisconnect()
        }
    }
    function peerConfirmation(page, list, ban) {
        return findChild(page, list ? ban ? "peerListBanConfirmationPopup" : "peerListDisconnectConfirmationPopup"
            : ban ? "banConfirmationPopup" : "disconnectConfirmationPopup")
    }
    function peerConfirmationButton(popup, list, ban, cancel) {
        const name = (list ? "peerList" : "") + (list ? ban ? "Ban" : "Disconnect" : ban ? "ban" : "disconnect")
            + (cancel ? "CancelButton" : "ConfirmButton")
        return waitForChild(popup.contentItem, name)
    }
    function physicallyClick(button) {
        verify(button !== null)
        tryVerify(function() { return button.visible && button.width > 0 && button.height > 0 })
        mouseClick(button, button.width / 2, button.height / 2)
    }

    function test_matching_async_ack_refreshes_exactly_once_and_wrong_operation_is_ignored() {
        nodeModel.autoCompletePeerAction = false
        const page = createPeerDetailsPage()
        page.requestDisconnect()
        const confirmation = findChild(page, "disconnectConfirmationPopup")
        tryCompare(confirmation, "opened", true)
        const confirm = waitForChild(testWindow.contentItem, "disconnectConfirmButton")
        verify(confirm !== null)
        confirm.clicked()
        const original = page.operation
        verify(original !== null)
        compare(original.pending, true)
        compare(original.connectionId, testPeerDetailsModel.connectionId)
        compare(nodeModel.disconnectPeerCalls, 1)
        compare(peerTableModel.refreshCalls, 0)
        compare(banListModel.refreshCalls, 0)
        const wrong = otherPeerDetailsModel.prepareDisconnect()
        verify(wrong.operationId !== original.operationId)
        verify(wrong.cancel())
        wrong.refresh()
        nodeModel.completePeerOperation(wrong.operationId)
        compare(page.operation, original)
        compare(original.pending, true)
        compare(peerTableModel.refreshCalls, 0)
        nodeModel.completePeerOperation(original.operationId)
        compare(original.pending, false)
        compare(original.resultPresent, true)
        compare(original.result.operationId, original.operationId)
        compare(peerTableModel.refreshCalls, 1)
        compare(banListModel.refreshCalls, 1)
        nodeModel.completePeerOperation(original.operationId)
        compare(nodeModel.disconnectPeerCalls, 1)
        compare(peerTableModel.refreshCalls, 1)
        compare(banListModel.refreshCalls, 1)
        wrong.retire()
    }

    function test_unrelated_legacy_notification_has_no_typed_refresh_owner() {
        nodeModel.autoCompletePeerAction = false
        const wrong = otherPeerDetailsModel.prepareDisconnect()
        verify(wrong.confirm())
        nodeModel.completePeerOperation(wrong.operationId)
        compare(peerTableModel.refreshCalls, 1)
        const page = createPeerDetailsPage()
        page.requestDisconnect()
        const original = page.operation
        verify(original.confirm())
        compare(nodeModel.disconnectPeerCalls, 2)
        compare(original.pending, true)
        // Replay a genuine other operation's compatibility notification. No
        // numeric ticket is guessed, and no mutation/result is fabricated.
        nodeModel.repeatPeerOperationNotification(wrong.operationId)
        compare(original.pending, true)
        compare(page.operation, original)
        compare(peerTableModel.refreshCalls, 1)
        compare(banListModel.refreshCalls, 1)
        nodeModel.completePeerOperation(original.operationId)
        compare(original.pending, false)
        compare(peerTableModel.refreshCalls, 2)
        compare(banListModel.refreshCalls, 2)
        compare(nodeModel.disconnectPeerCalls, 2)
        wrong.retire()
    }

    function test_non_rejected_uncertain_outcomes_refresh_authoritative_inventories_data() {
        return [
            { tag: "partially-applied", outcome: PeerCommandOperation.PartiallyApplied, error: "The ban was applied, but disconnecting the peer failed." },
            { tag: "uncertain", outcome: PeerCommandOperation.Uncertain, error: "Refresh the original peer and ban state." }
        ]
    }
    function test_non_rejected_uncertain_outcomes_refresh_authoritative_inventories(data) {
        nodeModel.autoCompletePeerAction = false
        const page = createPeerDetailsPage()
        openBanConfirmation(page, "peerBanDuration_3600")
        const confirm = waitForChild(testWindow.contentItem, "banConfirmButton")
        verify(confirm !== null)
        confirm.clicked()
        compare(nodeModel.banPeerCalls, 1)
        compare(peerTableModel.refreshCalls, 0)
        compare(banListModel.refreshCalls, 0)
        const original = page.operation
        verify(original !== null)
        compare(original.pending, true)
        nodeModel.completePeerOperationWithOutcome(original.operationId, data.outcome, data.error)
        compare(original.result.outcome, data.outcome)
        compare(original.outcomeKnown, data.outcome !== PeerCommandOperation.Uncertain)
        verifyPeerActionError(page, data.error)
        compare(peerTableModel.refreshCalls, 1)
        compare(banListModel.refreshCalls, 1)
        compare(nodeModel.banPeerCalls, 1)
    }

    function test_disconnect_success_refreshes_peer_table_without_error() {
        const page = createPeerDetailsPage()
        const button = findChild(page, "peerDisconnectButton")
        const confirmation = findChild(page, "disconnectConfirmationPopup")
        const popup = findChild(page, "peerActionErrorPopup")
        verify(button !== null)
        verify(confirmation !== null)
        verify(popup !== null)

        button.clicked()
        tryCompare(confirmation, "opened", true)
        compare(nodeModel.disconnectPeerCalls, 0)
        const confirm = waitForChild(testWindow.contentItem, "disconnectConfirmButton")
        verify(confirm !== null)
        confirm.clicked()

        compare(nodeModel.disconnectPeerCalls, 1)
        tryCompare(peerTableModel, "refreshCalls", 1)
        compare(popup.opened, false)
    }

    function test_peer_details_uses_segmented_sections_and_ellipsis_actions() {
        const page = createPeerDetailsPage()
        const contentFrame = findChild(page, "peerDetailsContentFrame")
        const sections = findChild(page, "peerDetailsSections")
        const blockSection = findChild(page, "peerDetailsSectionsOption_1")
        const blocks = findChild(page, "peerBlocksSection")
        const addresses = findChild(page, "peerAddressesSection")
        const actionsButton = findChild(page, "peerActionsButton")
        const actionsMenu = findChild(page, "peerActionsMenu")
        verify(sections !== null)
        verify(contentFrame !== null)
        verify(blockSection !== null)
        verify(blocks !== null)
        verify(addresses !== null)
        verify(actionsButton !== null)
        verify(actionsMenu !== null)
        verify(findChild(page, "peerCopyButton") === null)

        blockSection.clicked()
        compare(page.sectionIndex, 1)
        compare(blockSection.text, "Relay data")
        compare(blocks.visible, true)
        compare(addresses.visible, true)
        compare(page.informationRows[4].label, "Session ID")
        compare(page.informationRows[4].value, testPeerDetailsModel.sessionId)
        compare(page.blockRelayRows[3].label, "High bandwidth")
        compare(page.blockRelayRows[3].value, "Yes")
        compare(page.addressRelayRows[0].label, "Address relay")
        compare(page.addressRelayRows[0].value, "No")
        compare(page.addressRelayRows.length, 1)

        actionsButton.clicked()
        tryCompare(actionsMenu, "opened", true)
        verify(findChild(actionsMenu, "peerDisconnectButton") !== null)
        verify(findChild(actionsMenu, "peerBanDuration_3600") !== null)
        verify(findChild(actionsMenu, "peerBanDuration_86400") !== null)
        verify(findChild(actionsMenu, "peerBanDuration_604800") !== null)
        verify(findChild(actionsMenu, "peerBanDuration_31536000") !== null)
        verify(findChild(actionsMenu, "peerBanButton") === null)

        page.width = 1200
        wait(0)
        compare(contentFrame.width, 840)
        compare(contentFrame.x, 180)
    }

    function test_peer_details_text_is_selectable_and_read_only() {
        const page = createPeerDetailsPage()
        const table = findChild(page, "peerInformationTable")
        const key = waitForChild(table, "peerDetailKey_0")
        const value = waitForChild(table, "peerDetailValue_0")
        verify(key !== null)
        verify(value !== null)
        for (const field of [key, value]) {
            verify(field.readOnly)
            verify(field.selectByMouse)
            field.forceActiveFocus()
            field.selectAll()
            compare(field.selectedText, field.text)
            const original = field.text
            keyClick(Qt.Key_X)
            compare(field.text, original)
            field.deselect()
        }
        compare(value.text, testPeerDetailsModel.address)
    }

    function test_confirmation_keeps_original_target_data() {
        return [
            { tag: "ban-selection-changes", ban: true, replacement: otherPeerDetailsModel },
            { tag: "ban-peer-disappears", ban: true, replacement: null },
            { tag: "disconnect-selection-changes", ban: false, replacement: otherPeerDetailsModel },
            { tag: "disconnect-peer-disappears", ban: false, replacement: null }
        ]
    }

    function test_confirmation_keeps_original_target(data) {
        const page = createPeerDetailsPage()
        if (data.ban) page.requestBan(3600, "1 hour")
        else page.requestDisconnect()
        const popup = findChild(page, data.ban ? "banConfirmationPopup" : "disconnectConfirmationPopup")
        tryCompare(popup, "opened", true)
        const originalMessage = popup.message
        verify(originalMessage.indexOf(testPeerDetailsModel.address) >= 0)

        page.details = data.replacement
        compare(popup.message, originalMessage)
        const confirm = waitForChild(testWindow.contentItem, data.ban ? "banConfirmButton" : "disconnectConfirmButton")
        verify(confirm !== null)
        confirm.clicked()
        if (data.ban) {
            compare(nodeModel.banPeerCalls, 1)
            compare(nodeModel.lastBannedAddress, testPeerDetailsModel.rawAddress)
        } else {
            compare(nodeModel.disconnectPeerCalls, 1)
            compare(nodeModel.lastDisconnectedNodeId, testPeerDetailsModel.nodeId)
        }
    }

    function test_refresh_preserves_selection_in_unchanged_field() {
        const page = createPeerDetailsPage()
        page.sectionIndex = 2
        const table = findChild(page, "peerNetworkTrafficTable")
        const ping = waitForChild(table, "peerDetailValue_5")
        const sent = waitForChild(table, "peerDetailValue_3")
        verify(ping !== null)
        verify(sent !== null)
        ping.forceActiveFocus()
        ping.selectAll()
        compare(ping.selectedText, testPeerDetailsModel.pingTime)
        testPeerDetailsModel.bytesSent = "2.0 MiB"
        tryCompare(sent, "text", "2.0 MiB total")
        compare(findChild(table, "peerDetailValue_5"), ping)
        compare(ping.selectedText, testPeerDetailsModel.pingTime)
        compare(ping.activeFocus, true)
    }

    function test_direction_type_uses_translated_model_text() {
        const page = createPeerDetailsPage()
        testPeerDetailsModel.type = "Saliente Retransmisión completa"
        compare(page.informationRows[2].value, "Saliente Retransmisión completa")
        testPeerDetailsModel.type = "Entrante"
        compare(page.informationRows[2].value, "Entrante")
    }

    function test_peer_details_formats_missing_values() {
        const page = createTemporaryObject(peerDetailsComponent, testWindow.contentItem)
        verify(page !== null)
        compare(page.informationRows[5].value, "Default")
        compare(page.informationRows[10].value, "—")
        compare(page.blockRelayRows[0].value, "—")
        compare(page.trafficRows[6].value, "—")
        compare(page.trafficRows[5].value, testPeerDetailsModel.pingTime)
    }

    function test_peer_toolbar_tabs_into_list_with_focus_rings() {
        peerListModelProxy.setPeerCountForTest(1)
        const page = createPeersPage()
        page.clearFilters()
        waitForPreferences(page)
        const search = findChild(page, "peerSearchField")
        const sort = findChild(page, "peerSortButton")
        const filter = findChild(page, "peerFilterButton")
        const row = waitForChild(page, "peerListItem_7")
        compare(filter.visible, false)
        search.forceActiveFocus(Qt.TabFocusReason)
        keyClick(Qt.Key_Tab)
        tryCompare(sort, "activeFocus", true)
        const names = ["Direction", "Connection", "Network", "Transport"]
        for (const name of names) {
            const button = findChild(page, "peer" + name + "FilterButton")
            keyClick(Qt.Key_Tab)
            tryCompare(button, "activeFocus", true)
            compare(button.visualFocus, true)
        }
        keyClick(Qt.Key_Tab)
        tryCompare(row, "activeFocus", true)
        compare(row.visualFocus, true)
    }

    function test_filter_direction_is_optional_single_selection() {
        const page = createPeersPage()
        page.clearFilters()
        waitForPreferences(page)
        const button = findChild(page, "peerDirectionFilterButton")
        const menu = findChild(page, "peerDirectionFilterMenu")
        const picker = findChild(page, "peerDirectionFilterPicker")
        const count = findChild(page, "peerFilterButton")
        button.clicked()
        tryCompare(menu, "opened", true)
        compare(page.directionOptions.length, 2)
        picker.itemAtIndex(0).clicked()
        waitForPreferences(page)
        compare(peerListModelProxy.directionFilters.join(","), "inbound")
        compare(button.active, true)
        compare(count.visible, true)
        compare(count.count, 1)
        picker.itemAtIndex(1).clicked()
        waitForPreferences(page)
        compare(peerListModelProxy.directionFilters.join(","), "outbound")
        picker.itemAtIndex(1).clicked()
        waitForPreferences(page)
        compare(peerListModelProxy.directionFilters.length, 0)
        compare(count.visible, false)
        menu.close()
        page.clearFilters()
        waitForPreferences(page)
    }

    function test_filter_multiple_selection_and_all_data() {
        return [
            { tag: "connection", label: "Connection", property: "connectionTypeFilters", options: "connectionTypeOptions" },
            { tag: "network", label: "Network", property: "networkFilters", options: "networkOptions" },
            { tag: "transport", label: "Transport", property: "transportFilters", options: "transportOptions" }
        ]
    }

    function test_filter_multiple_selection_and_all(data) {
        const page = createPeersPage()
        page.clearFilters()
        waitForPreferences(page)
        const button = findChild(page, "peer" + data.label + "FilterButton")
        const menu = findChild(page, "peer" + data.label + "FilterMenu")
        const picker = findChild(page, "peer" + data.label + "FilterPicker")
        const all = findChild(page, "peer" + data.label + "AllPicker")
        const count = findChild(page, "peerFilterButton")
        button.clicked()
        tryCompare(menu, "opened", true)
        compare(all.itemAtIndex(0).selected, true)
        for (let i = 0; i < page[data.options].length; ++i) {
            picker.itemAtIndex(i).clicked()
            waitForPreferences(page)
            compare(menu.opened, true)
            if (i < page[data.options].length - 1) {
                compare(peerListModelProxy[data.property].length, i + 1)
                compare(picker.itemAtIndex(i).selected, true)
                compare(all.itemAtIndex(0).selected, false)
                compare(count.count, 1)
            }
        }
        compare(peerListModelProxy[data.property].length, 0)
        compare(all.itemAtIndex(0).selected, true)
        compare(button.active, false)
        picker.itemAtIndex(0).clicked()
        waitForPreferences(page)
        all.itemAtIndex(0).clicked()
        waitForPreferences(page)
        compare(peerListModelProxy[data.property].length, 0)
        picker.itemAtIndex(0).clicked()
        waitForPreferences(page)
        picker.itemAtIndex(0).clicked()
        waitForPreferences(page)
        compare(all.itemAtIndex(0).selected, true)
        menu.close()
        page.clearFilters()
        waitForPreferences(page)
    }

    function test_filter_count_clear_and_saved_multiselection() {
        const page = createPeersPage()
        page.clearFilters()
        waitForPreferences(page)
        page.toggleFilter("direction", "inbound")
        waitForPreferences(page)
        page.toggleFilter("connectionType", "full-relay")
        page.toggleFilter("connectionType", "manual")
        page.toggleFilter("network", "ipv4")
        page.toggleFilter("network", "onion")
        page.toggleFilter("transport", "v2")
        waitForPreferences(page)
        const count = findChild(page, "peerFilterButton")
        compare(count.count, 4)
        const settings = findChild(page, "peerFilterSettings")
        compare(settings.peerConnectionTypeFilters.join(","), "full-relay,manual")
        count.clicked()
        const menu = findChild(page, "peerClearFiltersMenu")
        tryCompare(menu, "opened", true)
        findChild(menu, "peerClearFiltersAction").clicked()
        waitForPreferences(page)
        compare(count.visible, false)
        compare(settings.peerConnectionTypeFilters.length, 0)
        compare(peerListModelProxy.directionFilters.length, 0)
        compare(peerListModelProxy.networkFilters.length, 0)
        compare(peerListModelProxy.transportFilters.length, 0)
    }

    function test_filter_buttons_wrap_below_search() {
        const page = createPeersPage()
        page.clearFilters()
        waitForPreferences(page)
        page.toggleFilter("direction", "inbound")
        waitForPreferences(page)
        const filters = findChild(page, "peerFilters")
        const search = findChild(page, "peerSearchBar")
        const count = findChild(page, "peerFilterButton")
        const direction = findChild(page, "peerDirectionFilterButton")
        const transport = findChild(page, "peerTransportFilterButton")
        for (const width of [320, 430, 900]) {
            page.width = width
            wait(0)
            verify(filters.y >= search.parent.y + search.parent.height)
            compare(count.x, 0)
            tryVerify(function() { return direction.x >= count.width })
            for (const name of ["Direction", "Connection", "Network", "Transport"]) {
                const button = findChild(page, "peer" + name + "FilterButton")
                tryVerify(function() { return button.x >= 0 && button.x + button.width <= filters.width + 1 })
            }
        }
        page.width = 320
        tryVerify(function() { return transport.y > direction.y })
        search.inputField.forceActiveFocus(Qt.TabFocusReason)
        keyClick(Qt.Key_Tab)
        keyClick(Qt.Key_Tab)
        tryCompare(count, "activeFocus", true)
        keyClick(Qt.Key_Tab)
        tryCompare(direction, "activeFocus", true)
        page.clearFilters()
        waitForPreferences(page)
    }

    function test_peer_row_places_traffic_on_its_own_colored_line() {
        peerListModelProxy.setPeerCountForTest(1)
        const page = createPeersPage()
        const sent = waitForChild(page, "peerListItem_7Sent")
        const received = waitForChild(page, "peerListItem_7Received")
        verify(sent !== null)
        verify(received !== null)

        compare(sent.text, "↑ 1 kB")
        compare(received.text, "↓ 2 kB")
        compare(sent.color, Theme.color.trafficSent)
        compare(received.color, Theme.color.trafficReceived)
        compare(Math.round(sent.y), Math.round(received.y))
        verify(received.x > sent.x + sent.width)
    }

    function test_peers_heading_description_and_regular_padding_are_stable() {
        const page = createPeersPage()
        const contentFrame = findChild(page, "peersContentFrame")
        const description = findChild(page, "peersDescriptionLabel")
        const listCard = findChild(page, "peerListCard")
        const connected = findChild(page, "connectedPeersLabel")
        const banned = findChild(page, "viewBannedPeersButton")
        verify(contentFrame !== null)
        verify(description !== null)
        verify(listCard !== null)
        verify(connected !== null)
        verify(banned !== null)
        compare(description.text, "Peers are nodes you exchange transaction data with.")
        const listBottom = listCard.mapToItem(page, 0, listCard.height)
        const connectedTop = connected.mapToItem(page, 0, 0)
        const bannedTop = banned.mapToItem(page, 0, 0)
        const connectedCenter = connected.mapToItem(page, connected.width / 2, 0)
        const bannedCenter = banned.mapToItem(page, banned.width / 2, 0)
        verify(connectedTop.y >= listBottom.y)
        verify(bannedTop.y > connectedTop.y)
        compare(Math.round(connectedCenter.x), Math.round(page.width / 2))
        compare(Math.round(bannedCenter.x), Math.round(page.width / 2))

        page.compact = false
        page.width = 449
        wait(0)
        compare(contentFrame.x, 24)
        compare(contentFrame.width, 401)

        page.width = 450
        wait(0)
        compare(contentFrame.x, 24)
        compare(contentFrame.width, 402)
    }

    function test_peer_detail_options_button_has_keyboard_focus_ring() {
        const page = createPeerDetailsPage()
        const button = findChild(page, "peerActionsButton")
        const ring = findChild(page, "peerActionsButtonFocusBorder")
        verify(button !== null)
        verify(ring !== null)

        button.forceActiveFocus(Qt.TabFocusReason)
        tryCompare(button, "visualFocus", true)
        tryCompare(ring, "visible", true)
    }

    function test_right_click_peer_row_opens_shared_actions_menu() {
        peerListModelProxy.setPeerCountForTest(1)
        const page = createPeersPage()
        const row = waitForChild(page, "peerListItem_7")
        const menu = findChild(page, "peerRowActionsMenu")
        verify(row !== null)
        verify(menu !== null)

        mouseClick(row, row.width / 2, row.height / 2, Qt.RightButton)

        tryCompare(menu, "opened", true)
        compare(page.contextPeerDetails, testPeerDetailsModel)
        verify(findChild(menu, "peerBanDuration_3600") !== null)
        verify(findChild(menu, "peerBanDuration_31536000") !== null)
        const disconnect = findChild(menu, "peerDisconnectButton")
        verify(disconnect !== null)
        disconnect.clicked()
        const confirmation = findChild(page, "peerListDisconnectConfirmationPopup")
        verify(confirmation !== null)
        tryCompare(confirmation, "opened", true)
        compare(nodeModel.disconnectPeerCalls, 0)
        const confirm = waitForChild(testWindow.contentItem, "peerListDisconnectConfirmButton")
        verify(confirm !== null)
        confirm.clicked()
        compare(nodeModel.disconnectPeerCalls, 1)
        tryCompare(peerTableModel, "refreshCalls", 1)
        tryCompare(confirmation, "opened", false)
        tryCompare(confirmation, "visible", false)
    }

    function test_right_click_peer_row_uses_ban_confirmation() {
        peerListModelProxy.setPeerCountForTest(1)
        const page = createPeersPage()
        const row = waitForChild(page, "peerListItem_7")
        const menu = findChild(page, "peerRowActionsMenu")
        const confirmation = findChild(page, "peerListBanConfirmationPopup")
        verify(row !== null)
        verify(menu !== null)
        verify(confirmation !== null)

        mouseClick(row, row.width / 2, row.height / 2, Qt.RightButton)
        tryCompare(menu, "opened", true)
        findChild(menu, "peerBanDuration_3600").clicked()
        tryCompare(confirmation, "opened", true)
        const confirm = waitForChild(testWindow.contentItem, "peerListBanConfirmButton")
        verify(confirm !== null)
        confirm.clicked()

        compare(nodeModel.banPeerCalls, 1)
        tryCompare(peerTableModel, "refreshCalls", 1)
        tryCompare(banListModel, "refreshCalls", 1)
        tryCompare(confirmation, "opened", false)
        tryCompare(confirmation, "visible", false)
    }

    function test_split_view_alert_is_centered_in_peers_container() {
        peerListModelProxy.setPeerCountForTest(1)
        const page = createPeersViewPage()
        const details = findChild(page, "peerDetails")
        verify(details !== null)
        tryCompare(details, "visible", true)
        const banDuration = findChild(details, "peerBanDuration_3600")
        const confirmation = findChild(details, "banConfirmationPopup")
        verify(banDuration !== null)
        verify(confirmation !== null)

        banDuration.clicked()

        tryCompare(confirmation, "opened", true)
        compare(confirmation.parent, page)
        compare(confirmation.x, Math.round((page.width - confirmation.width) / 2))
        compare(confirmation.y, Math.round((page.height - confirmation.height) / 2))
        confirmation.close()
        tryCompare(confirmation, "opened", false)
        tryCompare(confirmation, "visible", false)
    }

    function test_empty_wide_view_hides_details_and_explains_empty_state() {
        const page = createPeersViewPage()
        const details = findChild(page, "peerDetails")
        const emptyLabel = findChild(page, "noPeerDetailsLabel")
        verify(details !== null)
        verify(emptyLabel !== null)
        compare(details.visible, false)
        compare(emptyLabel.visible, true)
        compare(emptyLabel.text, "No peers connected")
    }

    function test_peers_offline_banner_follows_network_status() {
        const page = createPeersPage()
        const banner = findChild(page, "peersOfflineBanner")
        verify(banner !== null)
        compare(banner.visible, false)

        networkStatusModel.setNetworkOfflineForTest(true)
        tryCompare(banner, "visible", true)
    }

    function test_disconnect_failure_opens_error_popup() {
        nodeModel.disconnectPeerResult = false
        const page = createPeerDetailsPage()
        const button = findChild(page, "peerDisconnectButton")
        const confirmation = findChild(page, "disconnectConfirmationPopup")
        verify(button !== null)
        verify(confirmation !== null)

        button.clicked()
        tryCompare(confirmation, "opened", true)
        compare(nodeModel.disconnectPeerCalls, 0)
        const confirm = waitForChild(testWindow.contentItem, "disconnectConfirmButton")
        verify(confirm !== null)
        confirm.clicked()

        compare(nodeModel.disconnectPeerCalls, 1)
        compare(peerTableModel.refreshCalls, 0)
        verifyPeerActionError(page, "The peer could not be disconnected. It may already be disconnected.")
        compare(peerTableModel.refreshCalls, 0)
        compare(banListModel.refreshCalls, 0)
    }

    function test_ban_success_refreshes_peer_and_ban_lists_without_error() {
        const page = createPeerDetailsPage()
        const confirmation = openBanConfirmation(page, "peerBanDuration_86400")
        const button = findChild(testWindow.contentItem, "banConfirmButton")
        const popup = findChild(page, "peerActionErrorPopup")
        verify(button !== null)
        verify(popup !== null)

        button.clicked()

        compare(nodeModel.banPeerCalls, 1)
        compare(confirmation.opened, false)
        tryCompare(peerTableModel, "refreshCalls", 1)
        tryCompare(banListModel, "refreshCalls", 1)
        compare(popup.opened, false)
    }

    function test_ban_failure_opens_error_popup() {
        nodeModel.banPeerResult = false
        const page = createPeerDetailsPage()
        openBanConfirmation(page, "peerBanDuration_3600")
        const button = findChild(testWindow.contentItem, "banConfirmButton")
        verify(button !== null)

        button.clicked()

        compare(nodeModel.banPeerCalls, 1)
        compare(peerTableModel.refreshCalls, 0)
        compare(banListModel.refreshCalls, 0)
        verifyPeerActionError(page, "The peer could not be banned.")
        compare(peerTableModel.refreshCalls, 0)
        compare(banListModel.refreshCalls, 0)
    }

    function test_cancel_has_no_effect_and_retains_original_identity_data() {
        return [
            { tag: "disconnect-original", ban: false, change: false },
            { tag: "disconnect-selection-changed", ban: false, change: true },
            { tag: "ban-original", ban: true, change: false },
            { tag: "ban-selection-changed", ban: true, change: true }
        ]
    }
    function test_cancel_has_no_effect_and_retains_original_identity(data) {
        const page = createPeerDetailsPage()
        if (data.ban) page.requestBan(PeerCommandOperation.Day, "1 day")
        else page.requestDisconnect()
        const original = page.operation
        verify(original !== null)
        compare(original.connectionId, testPeerDetailsModel.connectionId)
        compare(original.nodeIdText, testPeerDetailsModel.nodeIdText)
        if (data.change) page.details = otherPeerDetailsModel
        const popup = findChild(page, data.ban ? "banConfirmationPopup" : "disconnectConfirmationPopup")
        tryCompare(popup, "opened", true)
        const cancel = waitForChild(testWindow.contentItem, data.ban ? "banCancelButton" : "disconnectCancelButton")
        verify(cancel !== null)
        cancel.clicked()
        compare(original.confirmationRequired, false)
        compare(original.result.outcome, PeerCommandOperation.Cancelled)
        compare(original.result.connectionId, testPeerDetailsModel.connectionId)
        compare(original.pending, false)
        compare(nodeModel.disconnectPeerCalls, 0)
        compare(nodeModel.banPeerCalls, 0)
        compare(peerTableModel.refreshCalls, 0)
        compare(banListModel.refreshCalls, 0)
    }

    function test_confirm_is_exactly_once_while_original_is_pending() {
        nodeModel.autoCompletePeerAction = false
        const page = createPeerDetailsPage()
        page.requestDisconnect()
        const original = page.operation
        verify(original.confirm())
        compare(original.pending, true)
        verify(!original.confirm())
        verify(!original.cancel())
        original.refresh()
        compare(nodeModel.disconnectPeerCalls, 1)
        compare(peerTableModel.refreshCalls, 0)
        nodeModel.completePeerOperation(original.operationId)
        compare(original.pending, false)
        compare(original.result.mutationAttempted, true)
        compare(original.result.disconnectConfirmed, true)
        verify(!original.confirm())
        compare(nodeModel.disconnectPeerCalls, 1)
        compare(peerTableModel.refreshCalls, 1)
    }

    function test_admitted_result_survives_details_change_data() {
        return [{ tag: "replacement", replacement: otherPeerDetailsModel }, { tag: "removed", replacement: null }]
    }
    function test_admitted_result_survives_details_change(data) {
        nodeModel.autoCompletePeerAction = false
        const page = createPeerDetailsPage()
        page.requestBan(PeerCommandOperation.Week, "1 week")
        const original = page.operation
        verify(original.confirm())
        page.details = data.replacement
        compare(page.operation, original)
        nodeModel.completePeerOperation(original.operationId)
        compare(original.pending, false)
        compare(original.result.address, testPeerDetailsModel.address)
        compare(original.result.connectionId, testPeerDetailsModel.connectionId)
        compare(nodeModel.lastBannedAddress, testPeerDetailsModel.rawAddress)
        compare(nodeModel.lastBanDuration, PeerCommandOperation.Week)
        compare(nodeModel.banPeerCalls, 1)
        compare(peerTableModel.refreshCalls, 1)
        compare(banListModel.refreshCalls, 1)
    }

    function test_destroyed_view_does_not_own_inventory_refresh_data() {
        return [{ tag: "details", list: false }, { tag: "row-context", list: true }]
    }
    function test_destroyed_view_does_not_own_inventory_refresh(data) {
        nodeModel.autoCompletePeerAction = false
        const page = data.list ? createPeersPage() : createPeerDetailsPage()
        if (data.list) {
            page.contextPeerDetails = testPeerDetailsModel
            page.requestContextPeerDisconnect()
        } else page.requestDisconnect()
        const original = page.operation
        const id = original.operationId
        verify(original.confirm())
        let notifications = 0
        const observe = function() { ++notifications }
        original.changed.connect(observe)
        page.destroy()
        wait(0)
        compare(notifications, 0)
        nodeModel.completePeerOperation(id)
        compare(original.resultPresent, true)
        compare(original.result.operationId, id)
        original.refresh()
        compare(notifications, 0)
        original.changed.disconnect(observe)
        compare(nodeModel.disconnectPeerCalls, 1)
        compare(peerTableModel.refreshCalls, 1)
        compare(banListModel.refreshCalls, 1)
        nodeModel.completePeerOperation(id)
        compare(peerTableModel.refreshCalls, 1)
        compare(banListModel.refreshCalls, 1)
    }

    function test_unknown_refresh_queries_original_without_replaying_mutation() {
        nodeModel.autoCompletePeerAction = false
        nodeModel.autoCompletePeerQuery = false
        const page = createPeerDetailsPage()
        page.requestBan(PeerCommandOperation.Hour, "1 hour")
        const original = page.operation
        const confirmation = peerConfirmation(page, false, true)
        tryCompare(confirmation, "opened", true)
        physicallyClick(peerConfirmationButton(confirmation, false, true, false))
        compare(original.pending, true)
        tryCompare(confirmation, "visible", false)
        nodeModel.completePeerOperationWithOutcome(original.operationId, PeerCommandOperation.Uncertain, "Original outcome unavailable")
        verifyPeerActionError(page, "Original outcome unavailable")
        compare(original.outcomeKnown, false)
        const close = waitForChild(testWindow.contentItem, "actionErrorCloseButton")
        physicallyClick(close)
        tryCompare(findChild(page, "peerActionErrorPopup"), "visible", false)
        page.details = otherPeerDetailsModel
        const reconcile = findChild(page, "peerCommandReconcileButton")
        verify(reconcile !== null)
        compare(reconcile.visible, true)
        physicallyClick(reconcile)
        compare(original.queryPending, true)
        compare(original.pending, true)
        compare(nodeModel.peerQueryCalls, 1)
        verify(!original.reconcile())
        compare(nodeModel.banPeerCalls, 1)
        nodeModel.completePeerQuery(original.operationId, false, true, "")
        compare(original.queryPending, false)
        compare(original.result.outcome, PeerCommandOperation.Uncertain)
        compare(original.outcomeKnown, false)
        compare(original.result.observationAvailable, true)
        compare(original.result.targetConnected, false)
        compare(original.result.targetBanned, true)
        compare(original.result.connectionId, testPeerDetailsModel.connectionId)
        compare(nodeModel.banPeerCalls, 1)
        compare(peerTableModel.refreshCalls, 1)
        compare(banListModel.refreshCalls, 1)
    }

    function test_query_failure_preserves_unknown_mutation() {
        nodeModel.autoCompletePeerAction = false
        nodeModel.autoCompletePeerQuery = false
        const page = createPeerDetailsPage()
        page.requestDisconnect()
        const original = page.operation
        verify(original.confirm())
        nodeModel.completePeerOperationWithOutcome(original.operationId, PeerCommandOperation.Uncertain, "Original outcome unavailable")
        verify(original.reconcile())
        nodeModel.completePeerQuery(original.operationId, false, false, "Original state could not be read")
        compare(original.result.outcome, PeerCommandOperation.Uncertain)
        compare(original.errorText, "Original outcome unavailable")
        compare(original.queryError, "Original state could not be read")
        compare(original.result.observationAvailable, false)
        compare(original.outcomeKnown, false)
        compare(nodeModel.disconnectPeerCalls, 1)
        compare(nodeModel.peerQueryCalls, 1)
        verifyPeerActionError(page, "Original state could not be read")
    }

    function test_confirmation_rejects_unavailable_backend_without_effect() {
        const page = createPeerDetailsPage()
        page.requestDisconnect()
        const original = page.operation
        nodeModel.networkActionReady = false
        compare(testPeerDetailsModel.canDisconnect, false)
        verify(!original.confirm())
        compare(original.pending, false)
        compare(original.result.outcome, PeerCommandOperation.Rejected)
        compare(original.result.mutationAttempted, false)
        compare(original.outcomeKnown, true)
        compare(nodeModel.disconnectPeerCalls, 0)
        compare(peerTableModel.refreshCalls, 0)
    }

    function test_disconnected_confirmation_rejects_original_connection() {
        const page = createPeerDetailsPage()
        page.requestDisconnect()
        const original = page.operation
        testPeerDetailsModel.triggerDisconnected()
        compare(testPeerDetailsModel.connected, false)
        compare(findChild(page, "peerActionsButton").enabled, false)
        verify(!original.confirm())
        compare(original.result.connectionId, testPeerDetailsModel.connectionId)
        compare(original.result.outcome, PeerCommandOperation.Rejected)
        compare(original.result.mutationAttempted, false)
        compare(nodeModel.disconnectPeerCalls, 0)
    }

    function test_selection_is_scoped_and_auto_selection_follows_layout() {
        peerListModelProxy.setPeerCountForTest(2)
        const first = createPeersViewPage()
        const second = createPeersViewPage()
        compare(first.selection.selectedDetails, testPeerDetailsModel)
        compare(second.selection.selectedDetails, testPeerDetailsModel)
        first.selectPeer(otherPeerDetailsModel)
        compare(first.selection.connectionId, otherPeerDetailsModel.connectionId)
        compare(second.selection.connectionId, testPeerDetailsModel.connectionId)
        first.width = 520
        tryCompare(first.selection, "autoSelectFirst", false)
        first.selection.clear()
        compare(first.selectedDetails, null)
        compare(second.selectedDetails, testPeerDetailsModel)
        first.width = 900
        tryCompare(first.selection, "autoSelectFirst", true)
        compare(first.selectedDetails, testPeerDetailsModel)
        compare(first.selection.connected, true)
    }

    function test_same_node_id_reconnection_cannot_retarget_confirmation() {
        peerListModelProxy.setPeerCountForTest(1)
        const page = createPeersViewPage()
        const detailsPage = findChild(page, "peerDetails")
        verify(detailsPage !== null)
        detailsPage.requestDisconnect()
        const original = detailsPage.operation
        const oldConnection = original.connectionId
        const oldNodeId = original.nodeIdText
        peerListModelProxy.replaceFirstConnectionForTest()
        verify(page.selectedDetails !== null)
        compare(page.selection.nodeIdText, oldNodeId)
        verify(page.selection.connectionId !== oldConnection)
        compare(detailsPage.operation, original)
        compare(original.connectionId, oldConnection)
        verify(!original.confirm())
        compare(original.result.connectionId, oldConnection)
        compare(original.result.mutationAttempted, false)
        compare(nodeModel.disconnectPeerCalls, 0)
    }

    function test_context_confirmation_keeps_original_target_data() {
        return [{ tag: "ban", ban: true }, { tag: "disconnect", ban: false }]
    }
    function test_context_confirmation_keeps_original_target(data) {
        nodeModel.autoCompletePeerAction = false
        const page = createPeersPage()
        page.contextPeerDetails = testPeerDetailsModel
        if (data.ban) page.requestContextPeerBan(PeerCommandOperation.Year, "1 year")
        else page.requestContextPeerDisconnect()
        const original = page.operation
        const popup = findChild(page, data.ban ? "peerListBanConfirmationPopup" : "peerListDisconnectConfirmationPopup")
        tryCompare(popup, "opened", true)
        const message = popup.message
        page.contextPeerDetails = otherPeerDetailsModel
        compare(popup.message, message)
        verify(original.confirm())
        nodeModel.completePeerOperation(original.operationId)
        compare(original.result.connectionId, testPeerDetailsModel.connectionId)
        if (data.ban) {
            compare(nodeModel.lastBannedAddress, testPeerDetailsModel.rawAddress)
            compare(nodeModel.lastBanDuration, PeerCommandOperation.Year)
        } else compare(nodeModel.lastDisconnectedNodeId, testPeerDetailsModel.nodeId)
        compare(peerTableModel.refreshCalls, 1)
    }

    function test_confirmation_cannot_be_replaced_by_another_action() {
        const page = createPeerDetailsPage()
        page.requestDisconnect()
        const original = page.operation
        page.details = otherPeerDetailsModel
        page.requestBan(PeerCommandOperation.Day, "1 day")
        compare(page.operation, original)
        compare(original.confirmationRequired, true)
        compare(original.connectionId, testPeerDetailsModel.connectionId)
        compare(nodeModel.banPeerCalls, 0)
        verify(original.cancel())
        compare(nodeModel.disconnectPeerCalls, 0)
    }

    function test_parent_admission_close_keeps_receipt_without_view_notification() {
        nodeModel.autoCompletePeerAction = false
        const page = createPeerDetailsPage()
        requestConfirmation(page, false, false, testPeerDetailsModel)
        const original = page.operation
        const popup = peerConfirmation(page, false, false)
        tryCompare(popup, "opened", true)
        physicallyClick(peerConfirmationButton(popup, false, false, false))
        tryCompare(popup, "visible", false)
        let notifications = 0
        const observe = function() { ++notifications }
        original.changed.connect(observe)
        nodeModel.closePeerCommandAdmission()
        compare(notifications, 0)
        nodeModel.completePeerOperationWithOutcome(original.operationId, PeerCommandOperation.Uncertain, "Original native receipt retained")
        original.refresh()
        compare(original.resultPresent, true)
        compare(original.result.connectionId, testPeerDetailsModel.connectionId)
        compare(original.result.outcome, PeerCommandOperation.Uncertain)
        compare(original.errorText, "Original native receipt retained")
        compare(notifications, 0)
        compare(findChild(page, "peerActionErrorPopup").visible, false)
        compare(nodeModel.disconnectPeerCalls, 1)
        compare(peerTableModel.refreshCalls, 0)
        compare(banListModel.refreshCalls, 0)
        original.changed.disconnect(observe)
    }

    function test_closed_prompt_cancels_only_original_and_allows_successor_data() {
        return [
            { tag: "details-disconnect-escape", list: false, ban: false, escape: true },
            { tag: "details-ban-escape", list: false, ban: true, escape: true },
            { tag: "context-disconnect-escape", list: true, ban: false, escape: true },
            { tag: "context-ban-escape", list: true, ban: true, escape: true },
            { tag: "details-disconnect-direct-close", list: false, ban: false, escape: false },
            { tag: "details-ban-direct-close", list: false, ban: true, escape: false },
            { tag: "context-disconnect-direct-close", list: true, ban: false, escape: false },
            { tag: "context-ban-direct-close", list: true, ban: true, escape: false }
        ]
    }
    function test_closed_prompt_cancels_only_original_and_allows_successor(data) {
        // The popup's exit transition is the window under test.
        Theme.motion.mode = "full"
        const page = data.list ? createPeersPage() : createPeerDetailsPage()
        requestConfirmation(page, data.list, data.ban, testPeerDetailsModel)
        const original = page.operation
        const popup = peerConfirmation(page, data.list, data.ban)
        tryCompare(popup, "opened", true)
        compare(popup.originalOperation, original)
        const message = popup.message
        if (data.escape) {
            popup.contentItem.forceActiveFocus(Qt.OtherFocusReason)
            keyClick(Qt.Key_Escape)
        } else popup.close()
        // Exit animation still owns the original prompt. A different action
        // cannot replace or reopen either confirmation in this same view.
        compare(popup.visible, true)
        requestConfirmation(page, data.list, !data.ban, otherPeerDetailsModel)
        compare(page.operation, original)
        compare(popup.originalOperation, original)
        compare(popup.message, message)
        compare(peerConfirmation(page, data.list, !data.ban).visible, false)
        tryCompare(popup, "visible", false)
        compare(original.confirmationRequired, false)
        compare(original.result.outcome, PeerCommandOperation.Cancelled)
        compare(original.result.connectionId, testPeerDetailsModel.connectionId)
        requestConfirmation(page, data.list, !data.ban, otherPeerDetailsModel)
        const successor = page.operation
        verify(successor.operationId !== original.operationId)
        const successorPopup = peerConfirmation(page, data.list, !data.ban)
        tryCompare(successorPopup, "opened", true)
        compare(successorPopup.originalOperation, successor)
        compare(successor.confirmationRequired, true)
        compare(popup.originalOperation, original)
        physicallyClick(peerConfirmationButton(successorPopup, data.list, !data.ban, true))
        tryCompare(successorPopup, "visible", false)
        compare(successor.result.outcome, PeerCommandOperation.Cancelled)
        compare(successor.result.connectionId, otherPeerDetailsModel.connectionId)
        compare(original.result.connectionId, testPeerDetailsModel.connectionId)
        compare(nodeModel.banPeerCalls, 0)
        compare(nodeModel.disconnectPeerCalls, 0)
        compare(peerTableModel.refreshCalls, 0)
        compare(banListModel.refreshCalls, 0)
    }

    function test_close_cancellation_cannot_clear_reentrant_successor_data() {
        return [{ tag: "details", list: false }, { tag: "context", list: true }]
    }
    function test_close_cancellation_cannot_clear_reentrant_successor(data) {
        // The popup's exit transition is the window under test.
        Theme.motion.mode = "full"
        const page = data.list ? createPeersPage() : createPeerDetailsPage()
        requestConfirmation(page, data.list, false, testPeerDetailsModel)
        const original = page.operation
        const popup = peerConfirmation(page, data.list, false)
        tryCompare(popup, "opened", true)
        let reopened = false
        const startSuccessor = function() {
            if (reopened || original.confirmationRequired || !original.resultPresent) return
            reopened = true
            requestConfirmation(page, data.list, true, otherPeerDetailsModel)
        }
        original.changed.connect(startSuccessor)
        popup.close()
        tryCompare(popup, "visible", false)
        tryVerify(function() { return reopened })
        original.changed.disconnect(startSuccessor)
        const successor = page.operation
        verify(successor.operationId !== original.operationId)
        const nextPopup = peerConfirmation(page, data.list, true)
        tryCompare(nextPopup, "opened", true)
        compare(popup.originalOperation, original)
        compare(nextPopup.originalOperation, successor)
        compare(original.result.outcome, PeerCommandOperation.Cancelled)
        compare(successor.confirmationRequired, true)
        physicallyClick(peerConfirmationButton(nextPopup, data.list, true, true))
        tryCompare(nextPopup, "visible", false)
        compare(successor.result.connectionId, otherPeerDetailsModel.connectionId)
        compare(successor.result.outcome, PeerCommandOperation.Cancelled)
        compare(nodeModel.disconnectPeerCalls, 0)
        compare(nodeModel.banPeerCalls, 0)
        compare(peerTableModel.refreshCalls, 0)
        compare(banListModel.refreshCalls, 0)
    }

    function test_exiting_accepted_prompt_cannot_confirm_successor_data() {
        return [
            { tag: "details-disconnect-then-ban", list: false, ban: false },
            { tag: "context-ban-then-disconnect", list: true, ban: true }
        ]
    }
    function test_exiting_accepted_prompt_cannot_confirm_successor(data) {
        // The popup's exit transition is the window under test.
        Theme.motion.mode = "full"
        nodeModel.autoCompletePeerAction = false
        const page = data.list ? createPeersPage() : createPeerDetailsPage()
        requestConfirmation(page, data.list, data.ban, testPeerDetailsModel)
        const original = page.operation
        const popup = peerConfirmation(page, data.list, data.ban)
        tryCompare(popup, "opened", true)
        const confirm = peerConfirmationButton(popup, data.list, data.ban, false)
        physicallyClick(confirm)
        compare(popup.visible, true)
        compare(original.pending, true)
        nodeModel.completePeerOperation(original.operationId)
        compare(original.pending, false)
        compare(original.result.outcome, PeerCommandOperation.Succeeded)
        const message = popup.message
        requestConfirmation(page, data.list, !data.ban, otherPeerDetailsModel)
        compare(page.operation, original)
        compare(popup.originalOperation, original)
        compare(popup.message, message)
        compare(confirm.enabled, false)
        physicallyClick(confirm)
        compare(nodeModel.banPeerCalls + nodeModel.disconnectPeerCalls, 1)
        compare(peerConfirmation(page, data.list, !data.ban).visible, false)
        tryCompare(popup, "visible", false)
        requestConfirmation(page, data.list, !data.ban, otherPeerDetailsModel)
        const successor = page.operation
        verify(successor.operationId !== original.operationId)
        const nextPopup = peerConfirmation(page, data.list, !data.ban)
        tryCompare(nextPopup, "opened", true)
        compare(popup.originalOperation, original)
        compare(nextPopup.originalOperation, successor)
        compare(successor.confirmationRequired, true)
        physicallyClick(peerConfirmationButton(nextPopup, data.list, !data.ban, false))
        nodeModel.completePeerOperation(successor.operationId)
        tryCompare(nextPopup, "visible", false)
        compare(original.result.connectionId, testPeerDetailsModel.connectionId)
        compare(successor.result.connectionId, otherPeerDetailsModel.connectionId)
        compare(original.result.outcome, PeerCommandOperation.Succeeded)
        compare(successor.result.outcome, PeerCommandOperation.Succeeded)
        compare(nodeModel.banPeerCalls, 1)
        compare(nodeModel.disconnectPeerCalls, 1)
        compare(peerTableModel.refreshCalls, 2)
        compare(banListModel.refreshCalls, 2)
    }

    function test_exiting_prompt_does_not_block_an_independent_view() {
        // The popup's exit transition is the window under test.
        Theme.motion.mode = "full"
        nodeModel.autoCompletePeerAction = false
        const first = createPeerDetailsPage()
        requestConfirmation(first, false, false, testPeerDetailsModel)
        const original = first.operation
        const firstPopup = peerConfirmation(first, false, false)
        tryCompare(firstPopup, "opened", true)
        physicallyClick(peerConfirmationButton(firstPopup, false, false, false))
        nodeModel.completePeerOperation(original.operationId)
        compare(firstPopup.visible, true)
        const second = createPeersPage()
        requestConfirmation(second, true, true, otherPeerDetailsModel)
        const independent = second.operation
        verify(independent !== null)
        verify(independent.operationId !== original.operationId)
        const nextPopup = peerConfirmation(second, true, true)
        tryCompare(nextPopup, "opened", true)
        tryCompare(firstPopup, "visible", false)
        compare(independent.confirmationRequired, true)
        compare(nextPopup.originalOperation, independent)
        physicallyClick(peerConfirmationButton(nextPopup, true, true, false))
        nodeModel.completePeerOperation(independent.operationId)
        tryCompare(nextPopup, "visible", false)
        compare(original.result.connectionId, testPeerDetailsModel.connectionId)
        compare(independent.result.connectionId, otherPeerDetailsModel.connectionId)
        compare(independent.result.outcome, PeerCommandOperation.Succeeded)
        compare(nodeModel.disconnectPeerCalls, 1)
        compare(nodeModel.banPeerCalls, 1)
        compare(peerTableModel.refreshCalls, 2)
        compare(banListModel.refreshCalls, 2)
    }

    function test_unban_success_leaves_refresh_to_the_model_without_error() {
        const page = createPeersPageWithBannedPopup()
        const button = waitForChild(testWindow.contentItem, "unbanButton_0")
        const popup = findChild(page, "unbanActionErrorPopup")
        verify(button !== null)
        verify(popup !== null)

        button.clicked()

        compare(banListModel.unbanCalls, 1)
        compare(banListModel.refreshCalls, 0)
        compare(popup.opened, false)
    }

    function test_unban_failure_opens_error_popup() {
        banListModel.unbanResult = false
        const page = createPeersPageWithBannedPopup()
        const button = waitForChild(testWindow.contentItem, "unbanButton_0")
        verify(button !== null)

        button.clicked()

        compare(banListModel.unbanCalls, 1)
        compare(banListModel.refreshCalls, 0)
        verifyUnbanActionError(page)
    }

    function test_unban_survives_synchronous_model_reset() {
        banListModel.resetOnUnban = true
        const page = createPeersPageWithBannedPopup()
        const button = waitForChild(testWindow.contentItem, "unbanButton_0")
        const popup = findChild(page, "unbanActionErrorPopup")
        verify(button !== null)
        verify(popup !== null)

        button.clicked()

        compare(banListModel.unbanCalls, 1)
        compare(banListModel.refreshCalls, 0)
        compare(popup.opened, false)
    }

    function test_unban_failure_with_synchronous_model_reset_opens_error_popup() {
        banListModel.resetOnUnban = true
        banListModel.unbanResult = false
        const page = createPeersPageWithBannedPopup()
        const button = waitForChild(testWindow.contentItem, "unbanButton_0")
        verify(button !== null)

        button.clicked()

        compare(banListModel.unbanCalls, 1)
        compare(banListModel.refreshCalls, 0)
        verifyUnbanActionError(page)
    }
}

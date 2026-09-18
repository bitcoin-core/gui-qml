// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.2
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

    function init() {
        testPeerDetailsModel.type = "Outbound Full Relay"
        testPeerDetailsModel.bytesSent = "1.0 MiB"
        nodeModel.resetPeerActionTestState()
        networkStatusModel.setNetworkOfflineForTest(false)
        peerTableModel.resetTestState()
        banListModel.resetTestState()
        peerListModelProxy.setPeerCountForTest(0)
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
        const message = findChild(popup, "actionErrorMessage")
        verify(message !== null)
        verify(message.text.indexOf(expectedText) >= 0)
    }

    function verifyUnbanActionError(page) {
        const popup = findChild(page, "unbanActionErrorPopup")
        verify(popup !== null)
        tryCompare(popup, "opened", true)
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
        compare(peerTableModel.refreshCalls, 1)
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
        const search = findChild(page, "peerSearchField")
        const sort = findChild(page, "peerSortButton")
        const filter = findChild(page, "peerFilterButton")
        const row = waitForChild(page, "peerListItem_0")
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
        const button = findChild(page, "peerDirectionFilterButton")
        const menu = findChild(page, "peerDirectionFilterMenu")
        const picker = findChild(page, "peerDirectionFilterPicker")
        const count = findChild(page, "peerFilterButton")
        button.clicked()
        tryCompare(menu, "opened", true)
        compare(page.directionOptions.length, 2)
        picker.itemAtIndex(0).clicked()
        compare(peerListModelProxy.directionFilters.join(","), "inbound")
        compare(button.active, true)
        compare(count.visible, true)
        compare(count.count, 1)
        picker.itemAtIndex(1).clicked()
        compare(peerListModelProxy.directionFilters.join(","), "outbound")
        picker.itemAtIndex(1).clicked()
        compare(peerListModelProxy.directionFilters.length, 0)
        compare(count.visible, false)
        menu.close()
        page.clearFilters()
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
        all.itemAtIndex(0).clicked()
        compare(peerListModelProxy[data.property].length, 0)
        picker.itemAtIndex(0).clicked()
        picker.itemAtIndex(0).clicked()
        compare(all.itemAtIndex(0).selected, true)
        menu.close()
        page.clearFilters()
    }

    function test_filter_count_clear_and_saved_multiselection() {
        const page = createPeersPage()
        page.clearFilters()
        page.setFilters("direction", ["inbound"])
        page.setFilters("connectionType", ["full-relay", "manual"])
        page.setFilters("network", ["ipv4", "onion"])
        page.setFilters("transport", ["v2"])
        const count = findChild(page, "peerFilterButton")
        compare(count.count, 4)
        const settings = findChild(page, "peerFilterSettings")
        compare(settings.peerConnectionTypeFilters, "full-relay,manual")
        compare(page.decodeFilter(settings.peerConnectionTypeFilters, page.connectionTypeOptions).join(","), "full-relay,manual")
        compare(page.decodeFilter("ipv4,garbage,onion,ipv4", page.networkOptions).join(","), "ipv4,onion")
        compare(page.decodeFilter("v1,v2", page.transportOptions).length, 0)
        compare(page.decodeFilter("inbound,outbound", page.directionOptions, true).join(","), "inbound")
        count.clicked()
        const menu = findChild(page, "peerClearFiltersMenu")
        tryCompare(menu, "opened", true)
        findChild(menu, "peerClearFiltersAction").clicked()
        compare(count.visible, false)
        compare(settings.peerConnectionTypeFilters, "")
        compare(peerListModelProxy.directionFilters.length, 0)
        compare(peerListModelProxy.networkFilters.length, 0)
        compare(peerListModelProxy.transportFilters.length, 0)
    }

    function test_filter_buttons_wrap_below_search() {
        const page = createPeersPage()
        page.clearFilters()
        page.setFilters("direction", ["inbound"])
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
    }

    function test_peer_row_places_traffic_on_its_own_colored_line() {
        peerListModelProxy.setPeerCountForTest(1)
        const page = createPeersPage()
        const sent = waitForChild(page, "peerListItem_0Sent")
        const received = waitForChild(page, "peerListItem_0Received")
        verify(sent !== null)
        verify(received !== null)

        compare(sent.text, "↑ 1 kB")
        compare(received.text, "↓ 2 kB")
        compare(sent.color, Theme.color.purple)
        compare(received.color, Theme.color.blue)
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
        const row = waitForChild(page, "peerListItem_0")
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
        compare(peerTableModel.refreshCalls, 1)
        tryCompare(confirmation, "opened", false)
        tryCompare(confirmation, "visible", false)
    }

    function test_right_click_peer_row_uses_ban_confirmation() {
        peerListModelProxy.setPeerCountForTest(1)
        const page = createPeersPage()
        const row = waitForChild(page, "peerListItem_0")
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
        compare(peerTableModel.refreshCalls, 1)
        compare(banListModel.refreshCalls, 1)
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
        verifyPeerActionError(page, "Could not disconnect peer.")
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
        compare(peerTableModel.refreshCalls, 1)
        compare(banListModel.refreshCalls, 1)
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
        verifyPeerActionError(page, "Could not ban peer.")
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

// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.2
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
        id: bannedPeersComponent

        BannedPeers {
            width: 460
            height: 680
        }
    }

    Component {
        id: peersComponent

        Peers {
            width: 460
            height: 680
        }
    }

    function init() {
        nodeModel.resetPeerActionTestState()
        networkStatusModel.setNetworkOfflineForTest(false)
        peerTableModel.resetTestState()
        banListModel.resetTestState()
    }

    function createPeerDetailsPage() {
        const page = createTemporaryObject(peerDetailsComponent, testWindow.contentItem)
        verify(page !== null)
        wait(0)
        return page
    }

    function createBannedPeersPage() {
        const page = createTemporaryObject(bannedPeersComponent, testWindow.contentItem)
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

    function test_disconnect_success_refreshes_peer_table_without_error() {
        const page = createPeerDetailsPage()
        const button = findChild(page, "peerDisconnectButton")
        const popup = findChild(page, "peerActionErrorPopup")
        verify(button !== null)
        verify(popup !== null)

        button.clicked()

        compare(nodeModel.disconnectPeerCalls, 1)
        compare(peerTableModel.refreshCalls, 1)
        compare(popup.opened, false)
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
        verify(button !== null)

        button.clicked()

        compare(nodeModel.disconnectPeerCalls, 1)
        compare(peerTableModel.refreshCalls, 0)
        verifyPeerActionError(page, "Could not disconnect peer.")
    }

    function test_ban_success_refreshes_peer_and_ban_lists_without_error() {
        const page = createPeerDetailsPage()
        const button = findChild(page, "banConfirmButton")
        const popup = findChild(page, "peerActionErrorPopup")
        verify(button !== null)
        verify(popup !== null)

        button.clicked()

        compare(nodeModel.banPeerCalls, 1)
        compare(peerTableModel.refreshCalls, 1)
        compare(banListModel.refreshCalls, 1)
        compare(popup.opened, false)
    }

    function test_ban_failure_opens_error_popup() {
        nodeModel.banPeerResult = false
        const page = createPeerDetailsPage()
        const button = findChild(page, "banConfirmButton")
        verify(button !== null)

        button.clicked()

        compare(nodeModel.banPeerCalls, 1)
        compare(peerTableModel.refreshCalls, 0)
        compare(banListModel.refreshCalls, 0)
        verifyPeerActionError(page, "Could not ban peer.")
    }

    function test_unban_success_leaves_refresh_to_the_model_without_error() {
        const page = createBannedPeersPage()
        const button = findChild(page, "unbanButton_0")
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
        const page = createBannedPeersPage()
        const button = findChild(page, "unbanButton_0")
        verify(button !== null)

        button.clicked()

        compare(banListModel.unbanCalls, 1)
        compare(banListModel.refreshCalls, 0)
        verifyUnbanActionError(page)
    }

    function test_unban_survives_synchronous_model_reset() {
        banListModel.resetOnUnban = true
        const page = createBannedPeersPage()
        const button = findChild(page, "unbanButton_0")
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
        const page = createBannedPeersPage()
        const button = findChild(page, "unbanButton_0")
        verify(button !== null)

        button.clicked()

        compare(banListModel.unbanCalls, 1)
        compare(banListModel.refreshCalls, 0)
        verifyUnbanActionError(page)
    }
}

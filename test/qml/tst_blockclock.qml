// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.2
import "../../qml/components"
import "../../qml/controls"

TestCase {
    name: "BlockClock"
    when: windowShown
    width: 800
    height: 700

    Window {
        id: presentationWindow
        width: 800
        height: 700
        visible: false
    }

    QtObject {
        id: nodeModelMock
        property int blockTipHeight: 0
        property int numPeers: 0
        property int numInboundPeers: 0
        property int numOutboundPeers: 0
        property int maxNumOutboundPeers: 10
        property int remainingSyncTime: 0
        property real verificationProgress: 0
        property bool initialSyncComplete: false
        property bool headerSyncActive: false
        property bool headerPresync: false
        property real headerSyncProgress: 0
        property bool networkActionReady: true
        property bool networkActionPending: false
        property string networkActionError: ""
        property bool pause: false
        property bool faulted: false
        property var warningList: []
        property int mempoolTransactionCount: 0
        property real mempoolUsageMB: 0
        property real mempoolMaxUsageMB: 0
        property int headerTipHeight: 0
        property real blockTipTime: 0
        property bool mempoolInfoPollingActive: false
        property int shutdownRequests: 0
        function requestShutdown() { shutdownRequests += 1 }
    }

    QtObject {
        id: networkStatusModelMock
        property bool networkOffline: false
    }

    QtObject {
        id: chainModelMock
        property string networkName: "REGTEST"
    }

    QtObject {
        id: blockClockModelMock
        property real currentTimeFraction: 0.25
        property var blockTimeFractions: []
        property int lastBlockAge: -1
    }

    Component {
        id: blockClockComponent
        BlockClock {
            parentWidth: 600
            parentHeight: 600
            nodeModelRef: nodeModelMock
            chainModelRef: chainModelMock
            blockClockModelRef: blockClockModelMock
            networkStatusModelRef: networkStatusModelMock
        }
    }

    Component {
        id: miniBlockClockComponent
        MiniBlockClock {
            iconSize: 18
            pageSelected: true
            nodeModelRef: nodeModelMock
            paused: true
            faulted: false
            networkStatusModelRef: networkStatusModelMock
            blockClockModelRef: blockClockModelMock
        }
    }

    Component {
        id: statusBarComponent
        NodeStatusBar {
            status: NodeStatus {
                nodeModelRef: nodeModelMock
                chainModelRef: chainModelMock
                blockClockModelRef: blockClockModelMock
                networkStatusModelRef: networkStatusModelMock
            }
        }
    }

    QtObject {
        id: trafficMock
        property bool active: false
        property real totalBytesReceived: 1234567890
        property real totalBytesSent: 0
        property var receivedRateList: [2345678, 1000]
        property var sentRateList: [41000]
    }

    Component {
        id: detailsComponent
        NodeDetailsPopup {
            trafficRef: trafficMock
            status: NodeStatus {
                nodeModelRef: nodeModelMock
                chainModelRef: chainModelMock
                blockClockModelRef: blockClockModelMock
                networkStatusModelRef: networkStatusModelMock
            }
        }
    }

    Component {
        id: toastComponent
        NewBlockToast {
            duration: 100
            status: NodeStatus {
                nodeModelRef: nodeModelMock
                chainModelRef: chainModelMock
                blockClockModelRef: blockClockModelMock
                networkStatusModelRef: networkStatusModelMock
            }
        }
    }

    Component {
        id: networkIndicatorComponent
        NetworkIndicator {
            chainModelRef: chainModelMock
        }
    }

    function resetMocks() {
        nodeModelMock.blockTipHeight = 0
        nodeModelMock.numPeers = 0
        nodeModelMock.numInboundPeers = 0
        nodeModelMock.numOutboundPeers = 0
        nodeModelMock.maxNumOutboundPeers = 10
        nodeModelMock.remainingSyncTime = 0
        nodeModelMock.verificationProgress = 0
        nodeModelMock.initialSyncComplete = false
        nodeModelMock.headerSyncActive = false
        nodeModelMock.headerPresync = false
        nodeModelMock.headerSyncProgress = 0
        nodeModelMock.networkActionReady = true
        nodeModelMock.networkActionPending = false
        nodeModelMock.networkActionError = ""
        nodeModelMock.pause = false
        nodeModelMock.faulted = false
        nodeModelMock.warningList = []
        nodeModelMock.mempoolTransactionCount = 0
        nodeModelMock.mempoolUsageMB = 0
        nodeModelMock.mempoolMaxUsageMB = 0
        nodeModelMock.headerTipHeight = 0
        nodeModelMock.blockTipTime = 0
        nodeModelMock.mempoolInfoPollingActive = false
        nodeModelMock.shutdownRequests = 0
        networkStatusModelMock.networkOffline = false
        chainModelMock.networkName = "REGTEST"
        blockClockModelMock.currentTimeFraction = 0.25
        blockClockModelMock.blockTimeFractions = []
        blockClockModelMock.lastBlockAge = -1
    }

    function createClock(properties) {
        const clock = createTemporaryObject(blockClockComponent, this, properties || {})
        verify(clock !== null)
        wait(0)
        return clock
    }

    function createMiniBlockClock() {
        const miniClock = createTemporaryObject(miniBlockClockComponent, this)
        verify(miniClock !== null)
        wait(0)
        return miniClock
    }

    function itemCenterX(item) {
        return item.x + item.width / 2
    }

    function itemCenterY(item) {
        return item.y + item.height / 2
    }

    function verifyCenteredInParent(item, parentItem) {
        verify(Math.abs(itemCenterX(item) - parentItem.width / 2) < 0.01)
        verify(Math.abs(itemCenterY(item) - parentItem.height / 2) < 0.01)
    }

    function verifyCenteredOn(item, targetItem) {
        verify(Math.abs(itemCenterX(item) - itemCenterX(targetItem)) < 0.01)
        verify(Math.abs(itemCenterY(item) - itemCenterY(targetItem)) < 0.01)
    }

    function clickClock(clock) {
        const area = findChild(clock, "blockClockDetailsArea")
        verify(area !== null)
        let requests = 0
        const count = function() { requests += 1 }
        clock.detailsRequested.connect(count)
        area.clicked(null)
        clock.detailsRequested.disconnect(count)
        return requests
    }

    function cleanup() {
        presentationWindow.visible = false
    }

    function createVisibleStatusBar() {
        presentationWindow.visible = true
        const bar = createTemporaryObject(statusBarComponent, presentationWindow.contentItem)
        verify(bar !== null)
        tryCompare(bar, "visible", true)
        return bar
    }

    function test_pause_control_waits_for_network_readiness() {
        resetMocks()
        nodeModelMock.networkActionReady = false
        const bar = createVisibleStatusBar()
        const primary = findChild(bar, "nodeStatusPrimaryAction")
        compare(primary.text, "Pause network")
        compare(primary.enabled, false)
        // The action also checks readiness when invoked directly.
        bar.trigger("pause")
        compare(bar.pauseConfirmation.opened, false)

        nodeModelMock.networkActionReady = true
        tryCompare(primary, "enabled", true)
        primary.clicked()
        tryCompare(bar.pauseConfirmation, "opened", true)
        // Readiness is checked again when the confirmation is accepted.
        nodeModelMock.networkActionReady = false
        bar.confirmPause()
        compare(nodeModelMock.pause, false)
        nodeModelMock.networkActionReady = true
        primary.clicked()
        tryCompare(bar.pauseConfirmation, "opened", true)
        bar.confirmPause()
        compare(nodeModelMock.pause, true)
        compare(primary.text, "Resume")

        // Shutdown revokes command availability even when nothing is pending.
        nodeModelMock.networkActionReady = false
        tryCompare(primary, "enabled", false)
        bar.trigger("resume")
        compare(nodeModelMock.pause, true)
    }

    function test_pending_network_action_blocks_duplicate_toggle() {
        resetMocks()
        const bar = createVisibleStatusBar()
        const primary = findChild(bar, "nodeStatusPrimaryAction")
        nodeModelMock.networkActionPending = true
        tryCompare(primary, "enabled", false)
        compare(primary.busy, true)
        bar.trigger("pause")
        compare(bar.pauseConfirmation.opened, false)
        nodeModelMock.networkActionPending = false
        tryCompare(primary, "enabled", true)
        compare(primary.busy, false)
        primary.clicked()
        tryCompare(bar.pauseConfirmation, "opened", true)
        bar.confirmPause()
        compare(nodeModelMock.pause, true)
    }

    function test_network_action_failure_is_visible() {
        resetMocks()
        const bar = createVisibleStatusBar()
        nodeModelMock.networkActionError = "Unable to change network activity."
        const popup = findChild(bar, "nodeStatusNetworkError")
        verify(popup !== null)
        tryCompare(popup, "visible", true)
        compare(popup.message, nodeModelMock.networkActionError)
        compare(nodeModelMock.pause, false)
        popup.close()
    }

    function test_connecting_state() {
        resetMocks()
        const clock = createClock()

        compare(clock.objectName, "blockClock")
        compare(clock.state, "CONNECTING")
        compare(clock.header, "Connecting")
        compare(clock.subText, "Finding peers")
        compare(clock.statusLabel, "Connecting")
        compare(clock.peersText, "No peers")
    }

    function test_ibd_state() {
        resetMocks()
        nodeModelMock.numPeers = 1
        nodeModelMock.numOutboundPeers = 1
        nodeModelMock.verificationProgress = 0.51
        nodeModelMock.remainingSyncTime = 360000

        const clock = createClock()

        compare(clock.state, "IBD")
        compare(clock.header, "51.0%")
        compare(clock.subText, "~6 minutes left")
        compare(clock.statusLabel, "Syncing blocks")
        compare(clock.peersText, "1 peer")
    }

    function test_header_sync_uses_block_clock_percentage_surface() {
        resetMocks()
        nodeModelMock.numPeers = 1
        nodeModelMock.numOutboundPeers = 1
        nodeModelMock.verificationProgress = 0.01
        nodeModelMock.headerSyncActive = true
        nodeModelMock.headerSyncProgress = 0.375

        const clock = createClock()

        compare(clock.state, "IBD")
        compare(clock.header, "37%")
        compare(clock.subText, "Syncing headers")
        compare(clock.statusLabel, "Syncing headers")
        compare(findChild(clock, "blockClockDial").syncProgress, 0.375)
    }

    function test_mini_and_full_clock_progress_match_data() {
        return [
            {tag: "headers", presync: false},
            {tag: "presync", presync: true}
        ]
    }

    function test_mini_and_full_clock_progress_match(data) {
        resetMocks()
        nodeModelMock.numPeers = 1
        nodeModelMock.verificationProgress = 0.01
        nodeModelMock.headerSyncActive = true
        nodeModelMock.headerPresync = data.presync
        nodeModelMock.headerSyncProgress = 0.375

        const clock = createClock()
        const miniClock = createMiniBlockClock()
        miniClock.pageSelected = false
        miniClock.paused = false
        const dial = findChild(clock, "blockClockDial")
        const miniDial = findChild(miniClock, "miniBlockClockDial")

        compare(miniClock.showIbdState, true)
        compare(miniDial.syncProgress, 0.375)
        compare(miniDial.syncProgress, dial.syncProgress)
        nodeModelMock.headerSyncProgress = 0.75
        compare(miniDial.syncProgress, 0.75)
        compare(miniDial.syncProgress, dial.syncProgress)

        // The selected navigation icon remains filled in either sync phase.
        miniClock.pageSelected = true
        compare(miniDial.syncProgress, 1.0)
        miniClock.pageSelected = false
        compare(miniDial.syncProgress, dial.syncProgress)

        nodeModelMock.headerSyncActive = false
        nodeModelMock.headerPresync = false
        nodeModelMock.verificationProgress = 0.42
        compare(miniDial.syncProgress, 0.42)
        compare(miniDial.syncProgress, dial.syncProgress)
    }

    function test_header_presync_subtext() {
        resetMocks()
        nodeModelMock.numPeers = 1
        nodeModelMock.numOutboundPeers = 1
        nodeModelMock.headerSyncActive = true
        nodeModelMock.headerPresync = true
        nodeModelMock.headerSyncProgress = 0.25

        const clock = createClock()

        compare(clock.state, "IBD")
        compare(clock.header, "25%")
        compare(clock.subText, "Pre-syncing headers")
    }

    function test_header_presync_subtext_fits_inside_dial() {
        resetMocks()
        nodeModelMock.numPeers = 1
        nodeModelMock.headerSyncActive = true
        nodeModelMock.headerPresync = true
        nodeModelMock.headerSyncProgress = 0.25

        for (const parentWidth of [600, 120]) {
            const clock = createClock({parentWidth})
            const subText = findChild(clock, "blockClockSubText")

            verify(subText !== null)
            compare(subText.text, "Pre-syncing headers")
            tryVerify(function() {
                return subText.paintedWidth <= subText.width
            }, 1000)
            verify(!subText.truncated,
                   "text was truncated at requested pixel size " + subText.font.pixelSize +
                   ", resolved pixel size " + subText.fontInfo.pixelSize +
                   ", width " + subText.width +
                   ", painted width " + subText.paintedWidth)
        }
    }

    function test_blockclock_state() {
        resetMocks()
        nodeModelMock.numPeers = 1
        nodeModelMock.numOutboundPeers = 1
        nodeModelMock.initialSyncComplete = true
        // Completion is authoritative even when the progress estimate has not
        // reached the old UI threshold.
        nodeModelMock.verificationProgress = 0.75
        nodeModelMock.blockTipHeight = 123456

        const clock = createClock()

        compare(clock.state, "BLOCKCLOCK")
        compare(clock.header, Number(nodeModelMock.blockTipHeight).toLocaleString(Qt.locale(), "f", 0))
        compare(clock.subText, "Block height")
        compare(clock.statusLabel, "Synced")
    }

    function test_blockclock_subtext_reports_last_block_age() {
        resetMocks()
        nodeModelMock.numPeers = 1
        nodeModelMock.initialSyncComplete = true
        const clock = createClock()

        blockClockModelMock.lastBlockAge = 30
        compare(clock.subText, "Last block just now")
        blockClockModelMock.lastBlockAge = 125
        compare(clock.subText, "Last block 2 min ago")
        blockClockModelMock.lastBlockAge = 3725
        compare(clock.subText, "Last block 1h 2m ago")
    }

    function test_synced_status_reports_warnings() {
        resetMocks()
        nodeModelMock.numPeers = 3
        nodeModelMock.initialSyncComplete = true
        const clock = createClock()

        compare(clock.statusLabel, "Synced")
        compare(clock.peersText, "3 peers")
        const link = findChild(clock, "blockClockWarnings")
        verify(link !== null)
        compare(link.shown, false)

        // The count is a separate link after the label, in any state.
        nodeModelMock.warningList = ["Unknown new rules activated"]
        compare(clock.statusLabel, "Synced")
        compare(clock.status.warningsText, "1 warning")
        compare(link.shown, true)
        compare(findChild(link, "warningsLinkText").text, "1 warning")
        nodeModelMock.warningList = ["One", "Two"]
        compare(findChild(link, "warningsLinkText").text, "2 warnings")
        nodeModelMock.initialSyncComplete = false
        nodeModelMock.verificationProgress = 0.5
        compare(clock.statusLabel, "Syncing blocks")
        compare(link.shown, true)

        let requests = 0
        clock.warningsRequested.connect(function() { requests += 1 })
        link.clicked()
        compare(requests, 1)
    }

    function test_layered_dial_follows_node_state() {
        resetMocks()
        const clock = createClock()
        const dial = findChild(clock, "blockClockDial")
        compare(dial.layeredRings, true)

        // Connecting greys out any earlier progress.
        compare(dial.dimmed, true)
        compare(dial.faulted, false)

        nodeModelMock.numPeers = 1
        nodeModelMock.headerSyncActive = true
        nodeModelMock.headerSyncProgress = 0.5
        nodeModelMock.verificationProgress = 0.01
        compare(dial.dimmed, false)
        compare(dial.headerProgress, 0.5)
        compare(dial.blockProgress, 0)

        nodeModelMock.headerSyncActive = false
        nodeModelMock.verificationProgress = 0.42
        // Genesis alone is not progress: Core reports a tiny fraction for it.
        compare(dial.headerProgress, 0)
        compare(dial.blockProgress, 0)
        nodeModelMock.blockTipHeight = 1200
        compare(dial.headerProgress, 1)
        compare(dial.blockProgress, 0.42)

        nodeModelMock.pause = true
        compare(dial.dimmed, true)
        nodeModelMock.pause = false

        nodeModelMock.faulted = true
        compare(dial.faulted, true)
        compare(clock.statusLabel, "Error")
        compare(clock.subText, "Node stopped")
    }

    function test_details_link_requests_node_details() {
        resetMocks()
        let clock = createClock()
        compare(findChild(clock, "blockClockDetailsLink").visible, false)

        clock = createClock({ showDetailsLink: true, showNetworkIndicator: false })
        const link = findChild(clock, "blockClockDetailsLink")
        verify(link !== null)
        compare(clock.showDetailsLink, true)
        compare(clock.height, link.y + link.height)
        let requests = 0
        clock.detailsRequested.connect(function() { requests += 1 })
        link.clicked()
        compare(requests, 1)
        compare(nodeModelMock.pause, false)
    }

    function test_core_ibd_state_overrides_rounded_verification_completion() {
        resetMocks()
        nodeModelMock.numPeers = 1
        nodeModelMock.initialSyncComplete = false
        nodeModelMock.verificationProgress = 0.9999

        const clock = createClock()
        const dial = findChild(clock, "blockClockDial")

        compare(clock.state, "IBD")
        compare(clock.synced, false)
        compare(clock.header, "99.9%")
        compare(dial.synced, false)
        compare(dial.syncProgress, 0.9999)
    }

    function test_startup_catch_up_uses_reported_progress() {
        resetMocks()
        nodeModelMock.numPeers = 1
        nodeModelMock.initialSyncComplete = false
        nodeModelMock.verificationProgress = 0.90

        const clock = createClock()
        const dial = findChild(clock, "blockClockDial")

        compare(clock.state, "IBD")
        compare(clock.header, "90.0%")
        compare(dial.syncProgress, 0.90)
    }

    function test_core_completion_allows_empty_current_period_history() {
        resetMocks()
        nodeModelMock.numPeers = 1
        nodeModelMock.initialSyncComplete = true
        nodeModelMock.verificationProgress = 0.75
        nodeModelMock.blockTipHeight = 123456
        blockClockModelMock.blockTimeFractions = []

        const clock = createClock()
        const dial = findChild(clock, "blockClockDial")

        compare(clock.state, "BLOCKCLOCK")
        compare(clock.synced, true)
        compare(dial.synced, true)
        compare(dial.blockTimeFractions.length, 0)
    }

    function test_core_completion_transition_updates_clock_state() {
        resetMocks()
        nodeModelMock.numPeers = 1
        nodeModelMock.verificationProgress = 1.0

        const clock = createClock()
        compare(clock.state, "IBD")
        compare(clock.header, "99.9%")

        nodeModelMock.initialSyncComplete = true
        wait(0)

        compare(clock.state, "BLOCKCLOCK")
        compare(clock.synced, true)
    }

    function test_completed_clocks_return_to_sync_during_catch_up() {
        resetMocks()
        nodeModelMock.numPeers = 1
        nodeModelMock.initialSyncComplete = true
        nodeModelMock.verificationProgress = 0.9999
        const clock = createClock()
        const miniClock = createMiniBlockClock()
        miniClock.pageSelected = false
        miniClock.paused = false
        const dial = findChild(clock, "blockClockDial")
        const miniDial = findChild(miniClock, "miniBlockClockDial")

        compare(clock.state, "BLOCKCLOCK")
        compare(miniClock.showClockState, true)
        nodeModelMock.initialSyncComplete = false
        compare(clock.state, "IBD")
        compare(clock.header, "99.9%")
        compare(miniClock.showIbdState, true)
        compare(dial.synced, false)
        compare(miniDial.synced, false)
        compare(miniDial.syncProgress, dial.syncProgress)

        nodeModelMock.initialSyncComplete = true
        compare(clock.state, "BLOCKCLOCK")
        compare(miniClock.showClockState, true)
        compare(dial.synced, true)
        compare(miniDial.synced, true)
    }

    function test_pause_state() {
        resetMocks()
        nodeModelMock.pause = true

        const clock = createClock()

        compare(clock.state, "PAUSE")
        compare(clock.header, "Paused")
        compare(clock.subText, "Network off")
    }

    function test_offline_state_overrides_connecting() {
        resetMocks()
        networkStatusModelMock.networkOffline = true

        const clock = createClock()

        compare(clock.state, "OFFLINE")
        compare(clock.header, "Offline")
        compare(clock.subText, "Check connection")
    }

    function test_offline_state_overrides_pause() {
        resetMocks()
        nodeModelMock.pause = true
        networkStatusModelMock.networkOffline = true

        const clock = createClock()

        compare(clock.state, "OFFLINE")
        compare(clock.header, "Offline")
        compare(clock.subText, "Check connection")
    }

    function test_mini_pause_icon_is_centered() {
        const miniClock = createMiniBlockClock()
        const pausedIcon = findChild(miniClock, "miniBlockClockPausedIcon")
        const pausedRing = findChild(miniClock, "miniBlockClockPausedRing")
        const pausedBars = findChild(miniClock, "miniBlockClockPausedBars")

        verify(pausedIcon !== null)
        verify(pausedRing !== null)
        verify(pausedBars !== null)
        compare(miniClock.paused, true)
        compare(miniClock.faulted, false)
        verifyCenteredInParent(pausedRing, pausedIcon)
        verifyCenteredOn(pausedBars, pausedRing)
    }

    function test_mini_offline_icon_is_visible() {
        resetMocks()
        networkStatusModelMock.networkOffline = true
        const miniClock = createMiniBlockClock()
        miniClock.paused = false
        wait(0)

        const offlineIcon = findChild(miniClock, "miniBlockClockOfflineIcon")
        verify(offlineIcon !== null)
        compare(miniClock.showOfflineState, true)
        compare(offlineIcon.source.toString(), "image://images/network-light")
    }

    function test_mini_offline_icon_overrides_pause() {
        resetMocks()
        networkStatusModelMock.networkOffline = true
        const miniClock = createMiniBlockClock()
        wait(0)

        const offlineIcon = findChild(miniClock, "miniBlockClockOfflineIcon")
        const pausedIcon = findChild(miniClock, "miniBlockClockPausedIcon")
        verify(offlineIcon !== null)
        verify(pausedIcon !== null)
        compare(miniClock.paused, true)
        compare(miniClock.offline, true)
        compare(miniClock.showOfflineState, true)
        compare(miniClock.showPausedState, false)
    }

    function test_mini_clock_uses_core_sync_completion() {
        resetMocks()
        nodeModelMock.numPeers = 1
        nodeModelMock.verificationProgress = 0.9999
        const miniClock = createMiniBlockClock()
        const dial = findChild(miniClock, "miniBlockClockDial")
        miniClock.pageSelected = false
        miniClock.paused = false
        wait(0)

        verify(dial !== null)
        compare(miniClock.showIbdState, true)
        compare(miniClock.showClockState, false)
        compare(dial.syncProgress, 0.9999)

        nodeModelMock.initialSyncComplete = true
        wait(0)

        compare(miniClock.showIbdState, false)
        compare(miniClock.showClockState, true)
        compare(dial.synced, true)
    }

    function test_error_state_still_opens_details() {
        resetMocks()
        nodeModelMock.numPeers = 1
        nodeModelMock.numOutboundPeers = 1
        nodeModelMock.verificationProgress = 1.0
        nodeModelMock.faulted = true

        const clock = createClock()

        compare(clock.state, "ERROR")
        compare(clock.header, "Error")
        compare(clickClock(clock), 1)
        compare(nodeModelMock.pause, false)
        compare(clock.state, "ERROR")
    }

    function test_click_opens_details_without_pausing() {
        resetMocks()
        const clock = createClock()

        compare(clickClock(clock), 1)
        compare(nodeModelMock.pause, false)
        compare(clock.state, "CONNECTING")
    }

    function test_inbound_only_peer_counts_as_connected_without_outbound_peer_fill() {
        resetMocks()
        nodeModelMock.numPeers = 1
        nodeModelMock.numInboundPeers = 1
        nodeModelMock.numOutboundPeers = 0
        nodeModelMock.verificationProgress = 0.51
        nodeModelMock.remainingSyncTime = 360000

        const clock = createClock()

        compare(clock.connected, true)
        compare(clock.state, "IBD")
        compare(clock.header, "51.0%")
    }

    function test_formatProgressPercentage_thresholds() {
        resetMocks()
        const clock = createClock()

        compare(clock.formatProgressPercentage(51), "51.0%")
        compare(clock.formatProgressPercentage(9.876), "9.88%")
        compare(clock.formatProgressPercentage(0.51), "0.51%")
        compare(clock.formatProgressPercentage(0.051), "0.05%")
        compare(clock.formatProgressPercentage(0.001), "0.00%")
        compare(clock.formatProgressPercentage(99.99, false), "99.9%")
        compare(clock.formatProgressPercentage(100, true), "100%")
    }

    function test_hidden_network_indicator_removes_extra_height() {
        resetMocks()
        let clock = createClock({ showNetworkIndicator: true })
        let indicator = findChild(clock, "blockClockNetworkIndicator")
        verify(indicator !== null)
        tryCompare(indicator, "state", "REGTEST")

        clock = createClock({ showNetworkIndicator: false })
        indicator = findChild(clock, "blockClockNetworkIndicator")
        verify(indicator !== null)
        verify(!indicator.visible)
        const statusLine = findChild(clock, "blockClockStatusLine")
        compare(clock.height, statusLine.y + statusLine.height)
    }

    function test_inactive_clock_retains_latest_model_snapshot_without_animations() {
        resetMocks()
        nodeModelMock.numPeers = 1
        nodeModelMock.verificationProgress = 0.5
        nodeModelMock.remainingSyncTime = 0
        presentationWindow.visible = true
        const clock = createTemporaryObject(blockClockComponent, presentationWindow.contentItem)
        verify(clock !== null)
        wait(0)
        const dial = findChild(clock, "blockClockDial")
        const estimatingAnimation = findChild(clock, "blockClockEstimatingAnimation")
        verify(dial !== null)
        verify(estimatingAnimation !== null)
        compare(clock.renderingActive, true)
        compare(clock.visible, true)
        compare(clock.windowVisible, true)
        compare(clock.presentationActive, true)
        compare(clock.estimating, true)
        tryCompare(estimatingAnimation, "running", true)

        clock.renderingActive = false
        compare(dial.renderingActive, false)
        tryCompare(estimatingAnimation, "running", false)

        blockClockModelMock.currentTimeFraction = 0.75
        blockClockModelMock.blockTimeFractions = [0.1, 0.2, 0.4]
        compare(dial.currentTimeFraction, 0.75)
        compare(dial.blockTimeFractions.length, 3)

        clock.renderingActive = true
        compare(dial.renderingActive, true)
        compare(dial.currentTimeFraction, 0.75)
        compare(dial.blockTimeFractions.length, 3)

        clock.visible = false
        compare(clock.presentationActive, false)
        compare(dial.renderingActive, false)
        tryCompare(estimatingAnimation, "running", false)

        clock.visible = true
        compare(clock.presentationActive, true)
        compare(dial.renderingActive, true)
        tryCompare(estimatingAnimation, "running", true)
        presentationWindow.visible = false
    }

    function test_hidden_window_suspends_clock_presentations() {
        resetMocks()
        nodeModelMock.numPeers = 1
        nodeModelMock.verificationProgress = 0.5
        nodeModelMock.remainingSyncTime = 0
        presentationWindow.visible = true

        const clock = createTemporaryObject(blockClockComponent, presentationWindow.contentItem)
        verify(clock !== null)
        const miniClock = createTemporaryObject(miniBlockClockComponent, presentationWindow.contentItem)
        verify(miniClock !== null)
        const dial = findChild(clock, "blockClockDial")
        const miniDial = findChild(miniClock, "miniBlockClockDial")
        const estimatingAnimation = findChild(clock, "blockClockEstimatingAnimation")
        verify(dial !== null)
        verify(miniDial !== null)
        verify(estimatingAnimation !== null)
        tryCompare(clock, "presentationActive", true)
        tryCompare(miniClock, "presentationActive", true)
        compare(miniDial.renderingActive, true)
        tryCompare(estimatingAnimation, "running", true)

        presentationWindow.visible = false
        tryCompare(clock, "presentationActive", false)
        tryCompare(miniClock, "presentationActive", false)
        compare(dial.renderingActive, false)
        compare(miniDial.renderingActive, false)
        tryCompare(estimatingAnimation, "running", false)

        presentationWindow.visible = true
        tryCompare(clock, "presentationActive", true)
        tryCompare(miniClock, "presentationActive", true)
        compare(dial.renderingActive, true)
        compare(miniDial.renderingActive, true)
        tryCompare(estimatingAnimation, "running", true)
        presentationWindow.visible = false
    }

    function formatted(value) {
        return Number(value).toLocaleString(Qt.locale(), "f", 0)
    }

    function test_status_bar_pause_requires_confirmation() {
        resetMocks()
        nodeModelMock.numPeers = 2
        const bar = createTemporaryObject(statusBarComponent, this)
        const primary = findChild(bar, "nodeStatusPrimaryAction")
        verify(primary !== null)
        compare(primary.text, "Pause network")
        compare(primary.accent, false)

        primary.clicked()
        tryCompare(bar.pauseConfirmation, "opened", true)
        compare(nodeModelMock.pause, false)
        compare(findChild(bar.pauseConfirmation.contentItem, "pauseNetworkConfirmationText").text,
                "Your node will disconnect from all 2 peers and stop receiving blocks and transactions until you resume. Sync progress is kept.")

        findChild(bar.pauseConfirmation.contentItem, "pauseNetworkCancelButton").clicked()
        tryCompare(bar.pauseConfirmation, "visible", false)
        compare(nodeModelMock.pause, false)

        primary.clicked()
        tryCompare(bar.pauseConfirmation, "opened", true)
        findChild(bar.pauseConfirmation.contentItem, "pauseNetworkConfirmButton").clicked()
        compare(nodeModelMock.pause, true)
        tryCompare(bar.pauseConfirmation, "visible", false)

        // Resuming needs no confirmation.
        compare(primary.text, "Resume")
        compare(primary.accent, true)
        primary.clicked()
        compare(nodeModelMock.pause, false)
    }

    function test_status_bar_actions_follow_state() {
        resetMocks()
        const bar = createTemporaryObject(statusBarComponent, this)
        const primary = findChild(bar, "nodeStatusPrimaryAction")
        const secondary = findChild(bar, "nodeStatusSecondaryAction")
        let routes = []
        bar.routeRequested.connect(function(action) { routes.push(action) })

        nodeModelMock.faulted = true
        compare(primary.text, "Shut down")
        compare(secondary.text, "View debug log")
        primary.clicked()
        compare(nodeModelMock.shutdownRequests, 1)
        secondary.clicked()
        compare(routes, ["debugLog"])

        nodeModelMock.faulted = false
        networkStatusModelMock.networkOffline = true
        compare(primary.text, "Network settings")
        compare(bar.status.secondaryAction, "")
        primary.clicked()
        compare(routes, ["debugLog", "networkSettings"])
        compare(findChild(bar, "nodeStatusBarLabel").text, "Offline")

        // The warnings link, and Node details' "warnings" action, ask the page
        // for the warnings list.
        let warningRequests = 0
        bar.warningsRequested.connect(function() { warningRequests += 1 })
        nodeModelMock.warningList = ["Clock skew"]
        const link = findChild(bar, "nodeStatusBarWarnings")
        compare(link.shown, true)
        link.clicked()
        bar.trigger("warnings")
        compare(warningRequests, 2)
    }

    function test_node_details_rows_and_actions() {
        resetMocks()
        nodeModelMock.numPeers = 13
        nodeModelMock.numOutboundPeers = 10
        nodeModelMock.numInboundPeers = 3
        nodeModelMock.initialSyncComplete = true
        nodeModelMock.verificationProgress = 1
        nodeModelMock.blockTipHeight = 915420
        nodeModelMock.mempoolTransactionCount = 23418
        nodeModelMock.mempoolUsageMB = 12.3
        nodeModelMock.mempoolMaxUsageMB = 300
        blockClockModelMock.lastBlockAge = 180
        const popup = createTemporaryObject(detailsComponent, this)
        // Rows are only computed while the popup is shown.
        compare(popup.rows.length, 0)
        popup.open()
        tryCompare(popup, "opened", true)

        const values = popup.rows.map(function(row) { return row.key + "=" + row.value })
        // Synced: only what the dial does not show, with the network named.
        compare(values, [
            "Block height=" + formatted(915420),
            "Mempool=" + formatted(23418) + " tx · 12 / 300 MB",
            "Peers=10 out · 3 in · Regtest"
        ])
        compare(findChild(popup.contentItem, "nodeDetailsValue_2").text, "10 out · 3 in · Regtest")
        // Opening the popup switched traffic sampling on.
        compare(trafficMock.active, true)

        // More details is collapsed until asked for.
        compare(popup.moreRows.length, 0)
        compare(popup.aboutRows.length, 0)
        const toggle = findChild(popup.contentItem, "nodeDetailsMoreToggle")
        const caret = findChild(popup.contentItem, "nodeDetailsMoreCaret")
        compare(toggle.text, "More details")
        compare(caret.rotation, 90)
        toggle.clicked()
        compare(popup.moreExpanded, true)
        // Expanded, the caret points back up.
        compare(toggle.text, "Fewer details")
        compare(caret.rotation, -90)
        compare(popup.moreRows.map(function(row) { return row.key + "=" + row.value }), [
            "Downloaded=1.2 GB",
            "Network speed=↓ 2.3 MB/s · ↑ 41.0 KB/s"
        ])

        // A new tip updates text in place; the row items are not rebuilt.
        const heightValue = findChild(popup.contentItem, "nodeDetailsValue_0")
        nodeModelMock.blockTipHeight = 915421
        compare(findChild(popup.contentItem, "nodeDetailsValue_0"), heightValue)
        compare(heightValue.text, formatted(915421))
        compare(nodeModelMock.mempoolInfoPollingActive, true)
        compare(findChild(popup.contentItem, "nodeDetailsStatus").text, "Synced")

        let actions = []
        popup.actionTriggered.connect(function(action) { actions.push(action) })
        findChild(popup.contentItem, "nodeDetailsPrimaryAction").clicked()
        compare(actions, ["pause"])
        tryCompare(popup, "visible", false)
        compare(nodeModelMock.mempoolInfoPollingActive, false)
        compare(trafficMock.active, false)
    }

    function test_new_node_rows_and_single_peer_pause_text() {
        resetMocks()
        // Only genesis: Core still reports a tiny verification progress.
        nodeModelMock.verificationProgress = 0.000001
        const popup = createTemporaryObject(detailsComponent, this)
        popup.open()
        tryCompare(popup, "opened", true)
        compare(popup.rows[0].key + "=" + popup.rows[0].value, "Progress=Not started")
        nodeModelMock.numPeers = 1
        compare(popup.status.pauseText,
                "Your node will disconnect from its peer and stop receiving blocks and transactions until you resume. Sync progress is kept.")
        popup.close()
    }

    function test_node_details_rows_while_syncing() {
        resetMocks()
        nodeModelMock.numPeers = 1
        nodeModelMock.numOutboundPeers = 1
        nodeModelMock.headerSyncActive = true
        nodeModelMock.headerSyncProgress = 0.64
        const popup = createTemporaryObject(detailsComponent, this)
        popup.open()
        tryCompare(popup, "opened", true)

        // Header phase: no block rows yet.
        const values = popup.rows.map(function(row) { return row.key + "=" + row.value })
        compare(values, [
            "Progress=Headers · 64%",
            "Peers=1 out · 0 in · Regtest"
        ])
        popup.moreExpanded = true
        compare(popup.moreRows.map(function(row) { return row.key }), ["Downloaded", "Network speed"])
    }

    function test_new_block_toast_only_for_live_tip() {
        resetMocks()
        nodeModelMock.numPeers = 1
        nodeModelMock.blockTipHeight = 100
        const toast = createTemporaryObject(toastComponent, this)

        // A block while syncing is not news.
        nodeModelMock.blockTipHeight = 101
        compare(toast.shown, false)

        nodeModelMock.initialSyncComplete = true
        nodeModelMock.blockTipHeight = 102
        compare(toast.shown, true)
        compare(findChild(toast, "newBlockToastText").text, "New block 102")
        tryCompare(toast, "shown", false)

        toast.active = false
        nodeModelMock.blockTipHeight = 103
        compare(toast.shown, false)

        toast.active = true
        nodeModelMock.pause = true
        nodeModelMock.blockTipHeight = 104
        compare(toast.shown, false)
    }

    function test_network_indicator_chip() {
        resetMocks()
        const indicator = createTemporaryObject(networkIndicatorComponent, this)
        compare(indicator.text, "Regtest")
        compare(indicator.chainColor, Theme.color.blue)

        chainModelMock.networkName = "SIGNET"
        compare(indicator.text, "Signet")
        compare(indicator.chainColor, Theme.color.purple)

        chainModelMock.networkName = "MAIN"
        compare(indicator.visible, false)
    }

    function test_node_details_leaves_traffic_sampling_it_did_not_start() {
        resetMocks()
        trafficMock.active = true
        const popup = createTemporaryObject(detailsComponent, this)
        popup.open()
        tryCompare(popup, "opened", true)
        popup.close()
        tryCompare(popup, "visible", false)
        // The settings page started sampling, so it stays on.
        compare(trafficMock.active, true)
        trafficMock.active = false
    }

    function test_node_details_rows_during_block_sync() {
        resetMocks()
        nodeModelMock.numPeers = 10
        nodeModelMock.numOutboundPeers = 10
        nodeModelMock.verificationProgress = 0.777
        nodeModelMock.blockTipHeight = 245120
        nodeModelMock.headerTipHeight = 262800
        nodeModelMock.blockTipTime = 1729555200
        nodeModelMock.remainingSyncTime = 3 * 3600 * 1000
        const popup = createTemporaryObject(detailsComponent, this)
        popup.open()
        tryCompare(popup, "opened", true)

        const values = popup.rows.map(function(row) { return row.key + "=" + row.value })
        // Progress merges blocks and transaction-weighted verification; the
        // dial's percentage and time left are not repeated.
        compare(values, [
            "Progress=" + formatted(245120) + " of " + formatted(262800) + " · 77.7% of transactions",
            "Speed=Measuring… · ↓ 2.3 MB/s",
            "Peers=10 out · 0 in · Regtest"
        ])

        // Speed is the average over the sampled minute.
        popup.syncSamples = [{ time: 0, height: 245000 }, { time: 60000, height: 245140 }]
        compare(popup.rows[1].value, "~140 blocks/min · ↓ 2.3 MB/s")
        popup.syncSamples = [{ time: 0, height: 245000 }, { time: 60000, height: 245004 }]
        compare(popup.rows[1].value, "~4.0 blocks/min · ↓ 2.3 MB/s")

        // The download rate is in Speed, so More details adds only the upload.
        popup.moreExpanded = true
        compare(popup.moreRows.map(function(row) { return row.key + "=" + row.value }), [
            "Tip block from=" + Qt.formatDate(new Date(1729555200 * 1000), "MMM yyyy"),
            "Downloaded=1.2 GB",
            "Upload speed=↑ 41.0 KB/s"
        ])

        // Samples and the expanded section are reset when the popup closes.
        popup.close()
        tryCompare(popup, "visible", false)
        compare(popup.syncSamples.length, 0)
        compare(popup.moreExpanded, false)
    }
}

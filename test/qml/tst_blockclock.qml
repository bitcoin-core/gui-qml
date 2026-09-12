// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.2
import "../../qml/components"

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
        property bool pause: false
        property bool faulted: false
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
        nodeModelMock.pause = false
        nodeModelMock.faulted = false
        networkStatusModelMock.networkOffline = false
        chainModelMock.networkName = "REGTEST"
        blockClockModelMock.currentTimeFraction = 0.25
        blockClockModelMock.blockTimeFractions = []
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

    function toggleArea(clock) {
        const area = findChild(clock, "blockClockToggleArea")
        verify(area !== null)
        return area
    }

    function clickToggle(clock) {
        clock.togglePause()
        wait(0)
    }

    function test_connecting_state() {
        resetMocks()
        const clock = createClock()

        compare(clock.objectName, "blockClock")
        compare(clock.state, "CONNECTING")
        compare(clock.header, "Connecting")
        compare(clock.subText, "Please wait")
    }

    function test_ibd_state() {
        resetMocks()
        nodeModelMock.numPeers = 1
        nodeModelMock.numOutboundPeers = 1
        nodeModelMock.verificationProgress = 0.51
        nodeModelMock.remainingSyncTime = 360000

        const clock = createClock()

        compare(clock.state, "IBD")
        compare(clock.header, "51%")
        compare(clock.subText, "~6 minutes left")
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
        compare(clock.header, "38%")
        compare(clock.subText, "Syncing headers")
        compare(findChild(clock, "blockClockDial").syncProgress, 0.375)
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
        compare(clock.subText, "Blocktime")
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
        compare(clock.header, "90%")
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

    function test_pause_state() {
        resetMocks()
        nodeModelMock.pause = true

        const clock = createClock()

        compare(clock.state, "PAUSE")
        compare(clock.header, "Paused")
        compare(clock.subText, "Tap to resume")
    }

    function test_offline_state_overrides_connecting() {
        resetMocks()
        networkStatusModelMock.networkOffline = true

        const clock = createClock()

        compare(clock.state, "OFFLINE")
        compare(clock.header, "Offline")
        compare(clock.subText, "Check network")
    }

    function test_offline_state_overrides_pause() {
        resetMocks()
        nodeModelMock.pause = true
        networkStatusModelMock.networkOffline = true

        const clock = createClock()

        compare(clock.state, "OFFLINE")
        compare(clock.header, "Offline")
        compare(clock.subText, "Check network")
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

    function test_error_state_overrides_and_disables_toggle() {
        resetMocks()
        nodeModelMock.numPeers = 1
        nodeModelMock.numOutboundPeers = 1
        nodeModelMock.verificationProgress = 1.0
        nodeModelMock.faulted = true

        const clock = createClock()
        const area = toggleArea(clock)

        compare(clock.state, "ERROR")
        compare(clock.header, "Error")
        clickToggle(clock)
        compare(nodeModelMock.pause, false)
        compare(clock.state, "ERROR")
    }

    function test_click_toggles_pause_model() {
        resetMocks()
        const clock = createClock()
        toggleArea(clock)

        clickToggle(clock)
        compare(nodeModelMock.pause, true)
        compare(clock.state, "PAUSE")

        clickToggle(clock)
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
        compare(clock.header, "51%")
    }

    function test_formatProgressPercentage_thresholds() {
        resetMocks()
        const clock = createClock()

        compare(clock.formatProgressPercentage(51), "51%")
        compare(clock.formatProgressPercentage(0.51), "0.5%")
        compare(clock.formatProgressPercentage(0.051), "0.05%")
        compare(clock.formatProgressPercentage(0.001), "0%")
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
        compare(clock.height, clock.width)
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
        const peers = findChild(clock, "blockClockPeersIndicator")
        verify(dial !== null)
        verify(estimatingAnimation !== null)
        verify(peers !== null)
        compare(clock.renderingActive, true)
        compare(clock.visible, true)
        compare(clock.windowVisible, true)
        compare(clock.presentationActive, true)
        compare(clock.estimating, true)
        tryCompare(estimatingAnimation, "running", true)

        clock.renderingActive = false
        compare(dial.renderingActive, false)
        compare(peers.active, false)
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
        compare(peers.active, false)
        tryCompare(estimatingAnimation, "running", false)

        clock.visible = true
        compare(clock.presentationActive, true)
        compare(dial.renderingActive, true)
        compare(peers.active, true)
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
        const peers = findChild(clock, "blockClockPeersIndicator")
        verify(dial !== null)
        verify(miniDial !== null)
        verify(estimatingAnimation !== null)
        verify(peers !== null)
        tryCompare(clock, "presentationActive", true)
        tryCompare(miniClock, "presentationActive", true)
        compare(miniDial.renderingActive, true)
        tryCompare(estimatingAnimation, "running", true)

        presentationWindow.visible = false
        tryCompare(clock, "presentationActive", false)
        tryCompare(miniClock, "presentationActive", false)
        compare(dial.renderingActive, false)
        compare(miniDial.renderingActive, false)
        compare(peers.presentationActive, false)
        tryCompare(estimatingAnimation, "running", false)

        presentationWindow.visible = true
        tryCompare(clock, "presentationActive", true)
        tryCompare(miniClock, "presentationActive", true)
        compare(dial.renderingActive, true)
        compare(miniDial.renderingActive, true)
        compare(peers.presentationActive, true)
        tryCompare(estimatingAnimation, "running", true)
        presentationWindow.visible = false
    }
}

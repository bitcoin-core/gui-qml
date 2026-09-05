// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtTest 1.2
import "../../qml/components"

TestCase {
    name: "BlockClock"
    when: windowShown
    width: 800
    height: 700

    QtObject {
        id: nodeModelMock
        property int blockTipHeight: 0
        property int numPeers: 0
        property int numInboundPeers: 0
        property int numOutboundPeers: 0
        property int maxNumOutboundPeers: 10
        property int remainingSyncTime: 0
        property real verificationProgress: 0
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
        property var timeRatioList: [0.25, 0.0]
        property string currentNetworkName: "REGTEST"
    }

    Component {
        id: blockClockComponent
        BlockClock {
            parentWidth: 600
            parentHeight: 600
            lifecycleModelRef: nodeModelMock
            syncModelRef: nodeModelMock
            networkModelRef: nodeModelMock
            chainModelRef: chainModelMock
            networkStatusModelRef: networkStatusModelMock
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
        nodeModelMock.headerSyncActive = false
        nodeModelMock.headerPresync = false
        nodeModelMock.headerSyncProgress = 0
        nodeModelMock.pause = false
        nodeModelMock.faulted = false
        networkStatusModelMock.networkOffline = false
        chainModelMock.timeRatioList = [0.25, 0.0]
        chainModelMock.currentNetworkName = "REGTEST"
    }

    function createClock(properties) {
        const clock = createTemporaryObject(blockClockComponent, this, properties || {})
        verify(clock !== null)
        wait(0)
        return clock
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

    function test_blockclock_state() {
        resetMocks()
        nodeModelMock.numPeers = 1
        nodeModelMock.numOutboundPeers = 1
        nodeModelMock.verificationProgress = 1.0
        nodeModelMock.blockTipHeight = 123456

        const clock = createClock()

        compare(clock.state, "BLOCKCLOCK")
        compare(clock.header, Number(nodeModelMock.blockTipHeight).toLocaleString(Qt.locale(), "f", 0))
        compare(clock.subText, "Blocktime")
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
}

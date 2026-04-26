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
        property int numOutboundPeers: 0
        property int maxNumOutboundPeers: 10
        property int remainingSyncTime: 0
        property real verificationProgress: 0
        property bool pause: false
        property bool faulted: false
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
            nodeModelRef: nodeModelMock
            chainModelRef: chainModelMock
        }
    }

    Component {
        id: miniBlockClockComponent
        MiniBlockClock {
            iconSize: 18
            pageSelected: true
            paused: true
            faulted: false
        }
    }

    function resetMocks() {
        nodeModelMock.blockTipHeight = 0
        nodeModelMock.numOutboundPeers = 0
        nodeModelMock.maxNumOutboundPeers = 10
        nodeModelMock.remainingSyncTime = 0
        nodeModelMock.verificationProgress = 0
        nodeModelMock.pause = false
        nodeModelMock.faulted = false
        chainModelMock.timeRatioList = [0.25, 0.0]
        chainModelMock.currentNetworkName = "REGTEST"
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
        nodeModelMock.numOutboundPeers = 1
        nodeModelMock.verificationProgress = 0.51
        nodeModelMock.remainingSyncTime = 360000

        const clock = createClock()

        compare(clock.state, "IBD")
        compare(clock.header, "51%")
        compare(clock.subText, "~6 minutes left")
    }

    function test_blockclock_state() {
        resetMocks()
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

    function test_error_state_overrides_and_disables_toggle() {
        resetMocks()
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

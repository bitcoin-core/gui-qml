// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtTest 1.2
import "../../qml/pages/node"
import "../../qml/controls"

RenderTestCase {
    id: testCase
    name: "NetworkTraffic"
    when: windowShown
    width: 1440; height: 1000; visible: true
    QtObject {
        id: trafficMock
        property bool active: false
        property var history: []
        property real totalBytesReceived: 1240000000
        property real totalBytesSent: 486000000
        property int lastWindowSize: 0
        function updateFilterWindowSize(size) { lastWindowSize = size }
    }
    Component {
        id: pageComponent
        Rectangle {
            height: page ? page.implicitHeight : 0
            color: Theme.color.background
            property alias page: page
            NetworkTraffic { id: page; width: parent.width; trafficModel: trafficMock }
        }
    }
    function init() {
        Theme.dark = true
        trafficMock.active = false
        trafficMock.history = [
            {time: 100000, received: 1000, sent: 100},
            {time: 101000, received: 3000, sent: 300},
            {time: 102000, received: 2000, sent: 200}
        ]
    }
    function cleanup() { Theme.dark = true }
    function createPage(width, overlay) {
        const fixture = createTemporaryObject(pageComponent, testCase, {width: width})
        const page = fixture.page
        page.trafficGraphScale = 300
        page.overlay = overlay
        waitForLayout(page)
        return page
    }
    function test_layouts_data() {
        return [
            {tag: "separate-dark", width: 1000, overlay: false, dark: true},
            {tag: "overlay-dark", width: 1000, overlay: true, dark: true},
            {tag: "separate-light", width: 1000, overlay: false, dark: false},
            {tag: "overlay-light", width: 1000, overlay: true, dark: false},
            {tag: "separate-narrow", width: 320, overlay: false, dark: true},
            {tag: "overlay-narrow", width: 320, overlay: true, dark: true}
        ]
    }
    function test_layouts(data) {
        Theme.dark = data.dark
        const page = createPage(data.width, data.overlay)
        verify(page !== null)
        waitForRendering(page)
        compare(trafficMock.active, true)
        const received = findChild(page, "networkTrafficReceivedGraph")
        const sent = findChild(page, "networkTrafficSentGraph")
        const combined = findChild(page, "networkTrafficOverlayGraph")
        compare(received.visible, !data.overlay)
        compare(sent.visible, !data.overlay)
        compare(combined.visible, data.overlay)
        compare(combined.lineChart.series.length, 2)
        compare(combined.yMaximum, received.yMaximum)
        verify(received.yMaximum > sent.yMaximum)
        compare(received.series[0].lineColor, Theme.color.blue)
        compare(sent.series[0].lineColor, Theme.color.purple)
        const total = findChild(page, "networkTrafficTotalReceived")
        verify(total.text.indexOf("GB") !== -1)
        const totals = findChild(page, "networkTrafficTotalsSection")
        verify(totals.y >= (data.overlay ? combined : sent).mapToItem(page, 0, (data.overlay ? combined : sent).height).y)
        verify(totals.width <= page.width)
        const mode = findChild(page, "networkTrafficModePicker")
        compare(mode.currentIndex, data.overlay ? 1 : 0)
        verify(Math.abs(mode.mapToItem(page, mode.width, 0).x - page.width) < 1)
        compare(page.formatRate(300).unit, page.formatRate(3000).unit)
        const graph = data.overlay ? combined : received
        verify(graph.lineChart.width <= graph.width)
        page.inspect(data.overlay ? "overlay" : "received", 101000)
        waitForLayout(page)
        const readout = findChild(graph, "networkTrafficReadout")
        verify(readout.visible)
        verify(readout.x >= 0 && readout.x + readout.width <= graph.width)
        verify(readout.y >= 0 && readout.y + readout.height <= graph.height)
    }
    function test_scrubbingSyncFreezeAndRangeChanges() {
        const page = createPage(1000, false)
        const received = findChild(page, "networkTrafficReceivedGraph")
        const sent = findChild(page, "networkTrafficSentGraph")
        received.lineChart.selectAt(received.lineChart.pixelX(101000))
        compare(page.selectedTime, 101000)
        compare(sent.lineChart.selectedX, 101000)
        compare(received.inspectedPoints[0].y, 3000)
        compare(received.inspectedPoints[1].y, 300)
        received.lineChart.forceActiveFocus()
        tryVerify(function() { return page.frozenHistory !== null })
        const oldEnd = page.endTime
        trafficMock.history = trafficMock.history.concat([{time: 103000, received: 9000, sent: 700}])
        compare(page.endTime, oldEnd)
        compare(received.inspectedPoints[0].y, 3000)
        keyClick(Qt.Key_Escape)
        compare(page.frozenHistory, null)
        compare(page.endTime, 103000)
        page.selectScale(3600)
        compare(trafficMock.lastWindowSize, 360)
        verify(!isFinite(page.selectedTime))
        page.active = false
        compare(trafficMock.active, false)
        page.active = true
        compare(trafficMock.active, true)
        page.destroy()
        wait(0)
        compare(trafficMock.active, false)
    }
    function test_modeSelectionAndHistoryOutsideWindow() {
        const page = createPage(1000, false)
        const mode = findChild(page, "networkTrafficModePicker")
        mouseClick(findChild(mode, "networkTrafficModePickerOption_1"))
        compare(page.overlay, true)
        compare(mode.currentIndex, 1)
        mouseClick(findChild(mode, "networkTrafficModePickerOption_0"))
        compare(page.overlay, false)
        trafficMock.history = [{time: -300000, received: 90000, sent: 80000}].concat(trafficMock.history)
        compare(page.visibleHistory.length, 3)
        compare(page.rateMaximum(0), 3000)
        const received = findChild(page, "networkTrafficReceivedGraph")
        page.inspect("received", 101500)
        compare(received.inspectedPoints[0].y, 3000)
        trafficMock.history = [{time: 100000, received: 1000, sent: 100}, {time: 110000, received: 2000, sent: 200}]
        page.inspect("received", 105000)
        compare(received.inspectedPoints[0], null)
        compare(received.inspectedPoints[1], null)
    }
    function test_emptyZeroAndMissingSamples() {
        trafficMock.history = []
        const page = createPage(320, true)
        verify(findChild(page, "networkTrafficEmptyStatus").visible)
        const combined = findChild(page, "networkTrafficOverlayGraph")
        verify(combined.yMaximum > 0)
        trafficMock.history = [{time: 1000, received: 0, sent: 0}, {time: 2000, received: null, sent: 100}]
        page.inspect("overlay", 2000)
        compare(combined.inspectedPoints[0], null)
        compare(combined.inspectedPoints[1].y, 100)
        verify(!findChild(page, "networkTrafficEmptyStatus").visible)
    }
}

// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
import QtQuick 2.15
import QtTest 1.2
import org.bitcoincore.qt 1.0
import "../../qml/pages/wallet"

TestCase {
    id: testCase
    name: "R10AddressIntents"
    when: windowShown
    width: 800
    height: 680
    Component { id: pageComponent; AddressList { width: 760; height: 620; wallet: testWalletModel } }
    Component { id: receiveSpy; SignalSpy { signalName: "receiveRequested" } }
    Component { id: requestSpy; SignalSpy { signalName: "paymentRequestRequested" } }
    function init() {
        testSetup.failOnWarnings()
        testWalletModel.available = true
        testSecondWalletModel.available = true
        testWalletModel.receiveRequests.resetRequestFixturesForTest()
        testWalletModel.addressListModel.resetForTest()
    }
    function cleanup() { testWalletModel.receiveRequests.resetRequestFixturesForTest() }
    function page() {
        const result = createTemporaryObject(pageComponent, testCase.Window.window.contentItem)
        verify(result !== null)
        result.selectedAddress = testWalletModel.addressListModel.addressAt(0)
        return result
    }
    function test_admission_and_cached_request_data() {
        return [
            {tag: "closed-no-callback", wallet: false, cached: false, callback: false},
            {tag: "closed-callback", wallet: false, cached: false, callback: true},
            {tag: "uncached-no-callback", wallet: true, cached: false, callback: false},
            {tag: "uncached-callback", wallet: true, cached: false, callback: true},
            {tag: "cached-no-callback", wallet: true, cached: true, callback: false},
            {tag: "cached-callback", wallet: true, cached: true, callback: true}
        ]
    }
    function test_admission_and_cached_request(data) {
        const view = page()
        const receive = createTemporaryObject(receiveSpy, this, {target: view})
        const request = createTemporaryObject(requestSpy, this, {target: view})
        if (!data.wallet) view.wallet = null
        if (data.cached) testWalletModel.receiveRequests.publishRequestForAddressForTest(view.selectedAddress, "req-1")
        const beforeRefresh = testWalletModel.addressListModel.refreshCalls
        let closed = 0
        view.createPaymentRequestFromSelected(data.callback ? function() { ++closed } : null)
        compare(closed, data.callback ? 1 : 0)
        compare(receive.count, 0)
        compare(request.count, data.cached ? 1 : 0)
        if (data.cached) {
            compare(request.signalArguments[0][0], "req-1")
            compare(view.errorText, "")
        } else {
            compare(view.errorText, "This address is no longer available.")
            compare(testWalletModel.addressListModel.refreshCalls, beforeRefresh + (data.wallet ? 1 : 0))
        }
    }
    function test_row_and_details_button_start_the_actual_intent() {
        const view = page()
        verify(waitForRendering(view))
        const list = findChild(view, "addressListView")
        verify(list !== null)
        const row = list.itemAtIndex(0)
        verify(row !== null)
        mouseClick(row, row.width - 12, row.height / 2)
        const popup = findChild(testCase.Window.window.contentItem, "addressDetailsPopup")
        verify(popup !== null)
        tryCompare(popup, "opened", true)
        const button = findChild(popup.contentItem, "addressDetailsCreatePaymentRequestButton")
        verify(button !== null)
        mouseClick(button)
        tryCompare(popup, "opened", false)
        compare(view.errorText, "This address is no longer available.")
    }
}

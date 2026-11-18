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
        testWalletModel.controlReceiveSelection(true)
        testSecondWalletModel.controlReceiveSelection(true)
        testPaymentRequest.clear()
        testWalletModel.receivingAddress.clear()
        testWalletModel.receivingAddressReady = false
        testWalletModel.addressListModel.resetForTest()
    }
    function cleanup() { testWalletModel.controlReceiveSelection(true); testSecondWalletModel.controlReceiveSelection(true); testWalletModel.receiveRequests.resetRequestFixturesForTest() }
    function page() {
        const result = createTemporaryObject(pageComponent, testCase.Window.window.contentItem)
        verify(result !== null)
        result.selectedAddress = testWalletModel.addressListModel.addressAt(0)
        return result
    }
    function test_admission_and_cached_request_data() {
        return [
            {tag: "closed-no-callback", wallet: false, admitted: true, cached: false, callback: false},
            {tag: "closed-callback", wallet: false, admitted: true, cached: false, callback: true},
            {tag: "refused-no-callback", wallet: true, admitted: false, cached: false, callback: false},
            {tag: "refused-callback", wallet: true, admitted: false, cached: false, callback: true},
            {tag: "cached-no-callback", wallet: true, admitted: true, cached: true, callback: false},
            {tag: "cached-callback", wallet: true, admitted: true, cached: true, callback: true}
        ]
    }
    function test_admission_and_cached_request(data) {
        const view = page()
        const receive = createTemporaryObject(receiveSpy, this, {target: view})
        const request = createTemporaryObject(requestSpy, this, {target: view})
        testWalletModel.controlReceiveSelection(data.admitted)
        if (!data.wallet) view.wallet = null
        if (data.cached) testWalletModel.receiveRequests.publishRequestForAddressForTest(view.selectedAddress, "req-1")
        const beforeRefresh = testWalletModel.addressListModel.refreshCalls
        let closed = 0
        view.createPaymentRequestFromSelected(data.callback ? function() { ++closed } : null)
        compare(closed, data.callback ? 1 : 0)
        compare(receive.count, 0)
        compare(request.count, data.cached ? 1 : 0)
        compare(testWalletModel.setReceiveAddressCalls, data.wallet && !data.cached ? 1 : 0)
        if (data.cached) {
            compare(request.signalArguments[0][0], "req-1")
            compare(view.errorText, "")
        } else {
            compare(view.errorText, "This address is no longer available.")
            compare(testWalletModel.addressListModel.refreshCalls, beforeRefresh + (data.wallet ? 1 : 0))
        }
    }
    function test_original_operation_waits_for_both_publications_data() {
        return [
            {tag: "address-outcome-first", loaded: false, outcomeFirst: true},
            {tag: "address-projection-first", loaded: false, outcomeFirst: false},
            {tag: "request-outcome-first", loaded: true, outcomeFirst: true},
            {tag: "request-projection-first", loaded: true, outcomeFirst: false}
        ]
    }
    function test_original_operation_waits_for_both_publications(data) {
        const view = page()
        const receive = createTemporaryObject(receiveSpy, this, {target: view})
        const request = createTemporaryObject(requestSpy, this, {target: view})
        let closed = 0
        view.createPaymentRequestFromSelected(function() { ++closed })
        const original = testWalletModel.selectedReceiveOperation()
        verify(original !== null)
        compare(original.pending, true)
        compare(testWalletModel.lastSelectedReceiveAddress, view.selectedAddress)
        view.createPaymentRequestFromSelected(function() { ++closed })
        compare(testWalletModel.setReceiveAddressCalls, 1)
        compare(closed, 1)
        function project() {
            if (data.loaded) testWalletModel.publishSelectedReceiveRequest("original-request")
            else testWalletModel.publishSelectedReceiveAddress()
        }
        function complete() { original.publishSelection(!data.loaded, data.loaded, data.loaded ? "original-request" : "", "") }
        if (data.outcomeFirst) complete()
        else project()
        compare(receive.count + request.count, 0)
        if (data.outcomeFirst) project()
        else complete()
        compare(receive.count, data.loaded ? 0 : 1)
        compare(request.count, data.loaded ? 1 : 0)
        if (data.loaded) compare(request.signalArguments[0][0], "original-request")
        original.changed()
        project()
        compare(receive.count + request.count, 1)
        compare(view.receiveOperationHandled, true)
    }
    function test_late_outcome_cannot_retarget_interest_data() {
        return [{tag: "hidden"}, {tag: "other-wallet"}, {tag: "no-wallet"}, {tag: "other-address"}]
    }
    function test_late_outcome_cannot_retarget_interest(data) {
        const view = page()
        const receive = createTemporaryObject(receiveSpy, this, {target: view})
        const request = createTemporaryObject(requestSpy, this, {target: view})
        const originalAddress = view.selectedAddress
        view.createPaymentRequestFromSelected(null)
        const original = testWalletModel.selectedReceiveOperation()
        if (data.tag === "hidden") view.visible = false
        else if (data.tag === "other-wallet") view.wallet = testSecondWalletModel
        else if (data.tag === "no-wallet") view.wallet = null
        else view.selectedAddress = testWalletModel.addressListModel.addressAt(1)
        testWalletModel.publishSelectedReceiveAddress()
        original.publishSelection(true, false, "", "")
        compare(original.addressReady, true)
        compare(testWalletModel.lastSelectedReceiveAddress, originalAddress)
        compare(testSecondWalletModel.setReceiveAddressCalls, 0)
        compare(receive.count + request.count, 0)
        compare(view.errorText, "")
    }
    function test_failed_outcome_keeps_original_error_data() {
        return [{tag: "explicit", error: "Original reservation failed"}, {tag: "fallback", error: ""}]
    }
    function test_failed_outcome_keeps_original_error(data) {
        const view = page()
        const receive = createTemporaryObject(receiveSpy, this, {target: view})
        const before = testWalletModel.addressListModel.refreshCalls
        view.createPaymentRequestFromSelected(null)
        const original = testWalletModel.selectedReceiveOperation()
        original.publishSelection(false, false, "", data.error)
        compare(view.errorText, data.error.length ? data.error : "This address is no longer available.")
        compare(testWalletModel.addressListModel.refreshCalls, before + 1)
        compare(receive.count, 0)
        original.changed()
        compare(testWalletModel.addressListModel.refreshCalls, before + 1)
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
        compare(testWalletModel.setReceiveAddressCalls, 1)
        compare(testWalletModel.lastSelectedReceiveAddress, testWalletModel.addressListModel.addressAt(0))
        tryCompare(popup, "opened", false)
    }
}

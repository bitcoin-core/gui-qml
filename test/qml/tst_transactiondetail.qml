// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtTest 1.2
import org.bitcoincore.qt 1.0
import "../../qml/controls"
import "../../qml/pages/wallet"

TestCase {
    id: testCase
    name: "TransactionDetail"
    when: windowShown
    visible: true
    width: 1180
    height: 1100
    FontLoader { source: "qrc:/fonts/bitcoincoresans/regular" }
    FontLoader { source: "qrc:/fonts/bitcoincoresans/semibold" }
    FontLoader { source: "qrc:/fonts/robotomono/regular" }
    Component { id: detailComponent; TransactionDetail { width: 1180; height: 1100 } }
    Component { id: activityComponent; ActivityList { width: 1180; height: 1100 } }
    SignalSpy { id: transactionSpy; signalName: "showTransaction" }

    function entry(id, amount, ownership, overrides) {
        return Object.assign({id: id, index: Number(id.split(":")[1]), kind: "output", amountSat: amount,
            amountKnown: amount !== null, amount: amount === null ? "" : (amount / 100000000).toFixed(8) + " BTC",
            address: "bcrt1qtestaddress01234567890123456789012", ownership: ownership,
            label: "", paymentRequests: []}, overrides || {})
    }
    function transaction(overrides) {
        return Object.assign({txid: "27c8263f6b9a0000000000000000000000000000000000000000419b4d41c441",
            activityId: "tx:detail", address: "bcrt1qtestaddress", requestId: "", isPending: true,
            isPendingRequest: false, hasPaymentRequest: false, paymentRequests: [], type: Transaction.SendToAddress,
            amount: "0.00075800 BTC", netAmountSat: -75800, label: "Dinner with friends",
            timestamp: new Date(2026, 8, 16, 10, 3).getTime() / 1000,
            activityType: TransactionActivityModel.Send, status: Transaction.Unconfirmed,
            statusKnown: true, depth: 0, isInactive: false, canBump: true, replacedByTxid: "",
            feeKnown: true, feeSat: 800, feeRateSatPerVb: 800 / 141, virtualSize: 141, signalsRbf: true,
            size: 222, weight: 561, version: 2, lockTime: 0,
            blocksToMaturity: 0, blockHeight: null, actions: [],
            flow: {inputs: [entry("input:0", 100000, "wallet", {kind: "input"})],
                outputs: [entry("output:0", 75000, "external", {label: "Dinner with friends"}),
                    entry("output:1", 24200, "wallet", {isChange: true})],
                complete: true, feeKnown: true, feeSat: 800, feeAmount: "0.00000800 BTC", inputCount: 1, outputCount: 2}
        }, overrides || {})
    }
    function init() {
        Theme.dark = true
        walletController.reset()
        walletController.initialized = true
        walletController.setSelectedWalletObject(testWalletModel)
        optionsModel.thirdPartyTransactionUrls = ""
        testWalletModel.lastLoadedPaymentRequestDetailId = ""
        testBumpModel.reset()
        transactionSpy.target = null
        transactionSpy.clear()
    }
    function createDetail(data, properties) {
        const page = createTemporaryObject(detailComponent, this,
            Object.assign({transactionData: data || transaction()}, properties || {}))
        verify(page !== null)
        waitForRendering(page)
        return page
    }

    function test_large_received_flow_arrives_after_opening_data() {
        return [{tag: "101_outputs", count: 101}, {tag: "1001_outputs", count: 1001}]
    }
    function test_large_received_flow_arrives_after_opening(data) {
        const snapshot = transaction({activityType: TransactionActivityModel.Receive, canBump: false,
            netAmountSat: 1000, amount: "+0.00001000 BTC", feeKnown: false, feeSat: null})
        snapshot.flow = {inputs: [entry("input:0", null, "external", {kind: "input"})],
            outputs: [], outputCount: data.count, inputCount: 1, complete: false, feeKnown: false}
        for (let i = 0; i < data.count; ++i)
            snapshot.flow.outputs.push(entry("output:" + i, 1000, i === 1 ? "wallet" : "external"))
        const page = createDetail({txid: snapshot.txid, detailsLoading: true})
        const flow = findChild(page, "transactionDetailFlow")
        let peakOutputs = 0
        flow.outputEntriesChanged.connect(function() { peakOutputs = Math.max(peakOutputs, flow.outputEntries.length) })
        const preview = Object.assign({}, snapshot, {detailsLoading: true, flow: Object.assign({}, snapshot.flow, {
            outputs: [{id: "non-wallet-outputs", kind: "output-group", outputCount: data.count - 1,
                ownership: "external", amountKnown: true, amountSat: (data.count - 1) * 1000,
                amount: String(data.count - 1) + "000 sats"}, snapshot.flow.outputs[1]]
        })})
        testTransactionActivityModel.setRows([preview])
        page.transactionData = testTransactionActivityModel.transactionDetails(snapshot.txid, true)
        verify(waitForRendering(page))
        compare(flow.outputEntries.length, 3)
        compare(flow.outputEntries[1].id, "output:1")
        testTransactionActivityModel.setRows([snapshot])
        const start = Date.now()
        // Exercise QVariantMap/List data crossing the real C++/QML boundary.
        page.transactionData = testTransactionActivityModel.transactionDetails(snapshot.txid, true)
        verify(waitForRendering(page))
        console.log("Received-flow render", data.count, "outputs:", Date.now() - start, "ms; peak visible outputs:", peakOutputs)
        compare(flow.outputEntries.length, 3)
        verify(peakOutputs <= 3, "Collapsed flows must never instantiate every output while their bindings update")
        verify(findChild(flow, "transactionFlowOutput_3") === null)
    }

    function test_confirmation_states_data() {
        return [{tag: "zero", depth: 0, color: Theme.color.neutral7, text: "Unconfirmed"},
            {tag: "one", depth: 1, color: Theme.color.amber, text: "1 confirmation"},
            {tag: "five", depth: 5, color: Theme.color.amber, text: "5 confirmations"},
            {tag: "six", depth: 6, color: Theme.color.green, text: "6 confirmations"}]
    }
    function test_confirmation_states(data) {
        const page = createDetail(transaction({depth: data.depth}))
        const pill = findChild(page, "transactionConfirmationPill")
        compare(pill.text, data.text)
        compare(pill.accentColor, data.color)
        compare(findChild(findChild(page, "transactionDetailIcon"), "activityPendingRing").visible, data.depth === 0)
        compare(findChild(page, "speedUpBanner").visible, data.depth === 0)
    }
    function test_replacement_and_status_updates() {
        const page = createDetail(transaction({isInactive: true, replacedByTxid: "new-transaction"}))
        transactionSpy.target = page
        verify(findChild(page, "replacedBanner").visible)
        verify(!findChild(page, "speedUpBanner").visible)
        compare(page.inactiveLabel, "Replaced")
        const replacementButton = findChild(page, "replacedBannerPrimaryButton")
        compare(replacementButton.background.border.width, 1)
        replacementButton.clicked()
        compare(transactionSpy.count, 1)
        compare(transactionSpy.signalArguments[0][0], "new-transaction")
        // A historical replacement marker must not override the original if it later confirms.
        page.transactionData = transaction({depth: 6, isInactive: false, replacedByTxid: "new-transaction", blockHeight: 1234})
        verify(!findChild(page, "replacedBanner").visible)
        compare(findChild(page, "transactionBlockRow").value, "1234")
        page.transactionData = transaction({statusKnown: false, canBump: false})
        compare(findChild(page, "transactionConfirmationPill").text, "Status unavailable")
        verify(!findChild(page, "speedUpBanner").visible)
        page.transactionData = transaction({status: Transaction.Conflicted, depth: -1, isInactive: true})
        compare(page.inactiveLabel, "Conflicted")
        compare(findChild(page, "transactionConfirmationPill").text, "0 confirmations")
    }
    function test_missing_fee_is_not_zero_and_coinbase_has_no_fee() {
        const data = transaction({feeKnown: false, feeSat: null, feeRateSatPerVb: null})
        data.flow.inputs[0].amountKnown = false
        data.flow.inputs[0].amountSat = null
        data.flow.complete = false
        data.flow.feeKnown = false
        data.flow.feeSat = null
        const page = createDetail(data)
        compare(findChild(page, "transactionFeeRow").value, "Unavailable")
        compare(findChild(page, "transactionFeeRateRow").value, "Unavailable")
        verify(findChild(page, "transactionFlowUnknownNotice").visible)
        data.flow.coinbase = true
        page.transactionData = Object.assign({}, data, {activityType: TransactionActivityModel.Mined,
            blocksToMaturity: 50, canBump: false})
        compare(findChild(page, "transactionFeeRow").value, "Not applicable")
        verify(findChild(page, "transactionMaturityBanner").visible)
    }
    function test_action_types_and_empty_notes_data() {
        return [{tag: "send", type: TransactionActivityModel.Send, label: "Sent", inputs: 1, outputs: 2},
            {tag: "receive", type: TransactionActivityModel.Receive, label: "Received", inputs: 1, outputs: 2},
            {tag: "multiple", type: TransactionActivityModel.Multiple, label: "Multiple actions", inputs: 2, outputs: 3},
            {tag: "consolidation", type: TransactionActivityModel.Consolidation, label: "Consolidation", inputs: 3, outputs: 1},
            {tag: "split", type: TransactionActivityModel.Split, label: "Split", inputs: 1, outputs: 3},
            {tag: "self_send", type: TransactionActivityModel.InternalTransfer, label: "Sent to yourself", inputs: 1, outputs: 2}]
    }
    function test_action_types_and_empty_notes(data) {
        const snapshot = transaction({activityType: data.type, label: ""})
        snapshot.flow.inputs = []
        snapshot.flow.outputs = []
        for (let i = 0; i < data.inputs; ++i) snapshot.flow.inputs.push(entry("input:" + i, 100000, "wallet"))
        for (let j = 0; j < data.outputs; ++j) snapshot.flow.outputs.push(entry("output:" + j, 1000, "wallet"))
        const page = createDetail(snapshot)
        compare(page.actionLabel, data.label)
        if (data.type === TransactionActivityModel.InternalTransfer) {
            compare(findChild(page, "transactionDetailIcon").iconSource.toString(), "qrc:/icons/activity-internal")
            compare(findChild(page, "transactionDetailIcon").accent, Theme.color.purple)
        }
        compare(findChild(page, "transactionDetailNote"), null)
        compare(findChild(page, "transactionDetailFlow").inputEntries.length, data.inputs)
        compare(findChild(page, "transactionDetailFlow").outputEntries.length, data.outputs + 1)
    }
    function test_output_navigation_keeps_wallet_impact_and_request_association() {
        const data = transaction({label: "", activityType: TransactionActivityModel.Multiple})
        data.flow.outputs[1].paymentRequests = [{requestId: "paid", noteSelf: "Rent"}]
        testTransactionActivityModel.setRows([data])
        const stack = createTemporaryObject(activityComponent, this)
        verify(stack !== null)
        stack.navigateToTransaction(data.txid, 1)
        tryCompare(stack, "depth", 2)
        const page = stack.currentItem
        compare(page.amount, data.amount)
        compare(findChild(page, "transactionDetailFlow").selectedEntryId, "output:1")
        compare(findChild(page, "transactionFlowOutput_1").requests[0].requestId, "paid")
        findChild(page, "transactionDetailFlow").paymentRequestRequested("paid")
        tryCompare(testWalletModel, "lastLoadedPaymentRequestDetailId", "paid")
        tryCompare(stack, "depth", 3)
        testTransactionActivityModel.setRows([Object.assign({}, data, {depth: 6})])
        stack.pop()
        tryCompare(stack, "depth", 2)
        tryCompare(stack.currentItem, "depth", 6)
    }
    function test_fee_bump_opens_existing_review() {
        const page = createDetail()
        const button = findChild(page, "speedUpBannerPrimaryButton")
        verify(button.width >= button.contentItem.implicitWidth + button.leftPadding + button.rightPadding)
        findChild(page, "speedUpBanner").primaryClicked()
        const overlay = findChild(page, "speedUpOverlay")
        tryCompare(overlay, "opened", true)
        verify(overlay.readyToConfirm)
        overlay.close()
    }
    function test_full_width_scroll_and_narrow_layout() {
        const page = createDetail(transaction(), {width: 390, height: 650})
        const scroll = findChild(page, "transactionDetailScroll")
        const back = findChild(page, "activityDetailsBackButton")
        const more = findChild(page, "transactionDetailMoreButton")
        const title = findChild(page, "transactionDetailTitle")
        compare(title.text, "Transaction")
        const headerPositions = [back, title, more].map(function(item) { return item.mapToItem(page, 0, 0).y })
        verify(scroll.mapToItem(page, 0, 0).y >= page.header.height)
        verify(title.mapToItem(page, 0, 0).x >= back.mapToItem(page, back.width, 0).x)
        verify(title.mapToItem(page, title.width, 0).x <= more.mapToItem(page, 0, 0).x)
        compare(scroll.width, page.width)
        verify(scroll.contentHeight > scroll.height)
        const flickable = scroll.contentItem
        mouseWheel(scroll, 2, 400, 0, -240)
        tryVerify(function() { return flickable.contentY > 0 })
        for (let i = 0; i < 3; ++i) compare([back, title, more][i].mapToItem(page, 0, 0).y, headerPositions[i])
        mouseClick(more)
        const menu = findChild(page, "transactionDetailMoreMenu")
        tryCompare(menu, "opened", true)
        menu.close()
        tryCompare(menu, "visible", false)
        verify(findChild(page, "transactionFlowViewport").contentWidth > page.detailContentWidth)
        compare(findChild(page, "transactionDetailFlow").width, page.detailContentWidth)
        page.detailsExpanded = true
        waitForRendering(page)
        compare(findChild(page, "transactionOverviewGrid").columns, 1)
        compare(findChild(page, "transactionDetailsGrid").columns, 1)
        page.width = 720
        verify(waitForRendering(page))
        compare(findChild(page, "transactionOverviewGrid").columns, 2)
        compare(findChild(page, "transactionDetailsGrid").columns, 2)
    }
    function test_details_toggle_reveals_raw_transaction() {
        const raw = "02000000" + "abcdef0123456789".repeat(1000)
        const page = createDetail(transaction({rawTransaction: raw}))
        const toggle = findChild(page, "transactionDetailsToggle")
        const overview = findChild(page, "transactionOverviewSection")
        const technical = findChild(page, "transactionTechnicalDetailsSection")
        const section = findChild(technical, "transactionRawRow")
        compare(toggle.text, "Show details")
        verify(!overview.visible && !technical.visible && !section.visible)
        toggle.clicked()
        compare(toggle.text, "Hide details")
        verify(toggle.opened)
        verify(overview.visible && technical.visible && section.visible)
        compare(section.width, findChild(page, "transactionDetailsGrid").width)
        const text = findChild(page, "transactionRawText")
        compare(text.text, raw)
        verify(text.readOnly)
        const scroll = findChild(page, "transactionRawScroll")
        tryVerify(function() { return text.contentHeight > scroll.height })
        verify(text.width <= scroll.width)
        const copy = findChild(page, "transactionRawCopyButton")
        verify(copy.enabled)
        copy.clicked()
        verify(copy.copied)
        compare(Clipboard.text(), raw)
        toggle.clicked()
        verify(!overview.visible && !technical.visible && !section.visible)
        compare(text.text, "")
        page.transactionData = transaction({rawTransaction: ""})
        toggle.clicked()
        verify(!copy.enabled)
    }

    function test_overview_and_details_show_transaction_metadata() {
        const page = createDetail(transaction({timestamp: new Date(2026, 8, 15, 13, 46, 59).getTime() / 1000}))
        page.detailsExpanded = true
        waitForRendering(page)
        const overview = findChild(page, "transactionOverviewSection")
        const details = findChild(page, "transactionTechnicalDetailsSection")
        compare(findChild(page, "transactionOverviewGrid").columns, 4)
        compare(findChild(page, "transactionDetailsGrid").columns, 3)
        compare(findChild(overview, "transactionTimestampRow").loadedBodyItem.text, "2026-09-15 13:46:59")
        compare(findChild(overview, "transactionBlockRow").loadedBodyItem.text, "Not mined")
        compare(findChild(details, "transactionSizeRow").loadedBodyItem.text, "222 bytes")
        compare(findChild(details, "transactionVirtualSizeRow").loadedBodyItem.text, "141 vB")
        compare(findChild(details, "transactionWeightRow").loadedBodyItem.text, "561 WU")
        compare(findChild(details, "transactionVersionRow").loadedBodyItem.text, "2")
        compare(findChild(details, "transactionLockTimeRow").loadedBodyItem.text, "0")
        compare(findChild(details, "transactionRbfRow").loadedBodyItem.text, "Signaled")
        const id = findChild(overview, "transactionIdValue")
        compare(id.address, page.txid)
        verify(id.width < findChild(overview, "transactionIdRow").availableWidth)
        page.transactionData = transaction({size: null, weight: null, version: null, lockTime: null})
        for (const key of ["Size", "Weight", "Version", "LockTime"]) {
            compare(findChild(details, "transaction" + key + "Row").loadedBodyItem.text, "Unavailable")
        }
    }
}

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
    name: "ActivityList"
    when: windowShown
    visible: true
    width: 1180
    height: 950

    FontLoader { source: "qrc:/fonts/bitcoincoresans/regular" }
    FontLoader { source: "qrc:/fonts/bitcoincoresans/semibold" }
    FontLoader { source: "qrc:/fonts/robotomono/regular" }

    Component { id: pageComponent; ActivityList { width: 1180; height: 950 } }

    Component {
        id: iconImageComponent
        Image { width: 24; height: 24; sourceSize: Qt.size(48, 48) }
    }

    function test_activity_svg_resources_load_data() {
        return ["activity-send", "activity-receive", "activity-multiple", "activity-payment-request",
            "activity-consolidation", "activity-split", "activity-internal", "activity-invoice",
            "check", "coinbase", "file"].map(function(name) { return { tag: name, name: name } })
    }

    function test_activity_svg_resources_load(data) {
        const icon = createTemporaryObject(iconImageComponent, this, { source: "qrc:/icons/" + data.name + ".svg" })
        tryCompare(icon, "status", Image.Ready)
        verify(icon.paintedWidth > 0 && icon.paintedHeight > 0)
    }

    function action(id, label, amount, direction, address) {
        return {actionId: id, label: label, address: address || "bc1qrecipient42mx9jhyu82sk7la0q4x3g9lr8j0c2s",
            amount: amount, direction: direction, source: TransactionActivityModel.Output,
            outputIndex: id === "robert" ? 0 : 1, hasPaymentRequest: false, paymentRequests: []}
    }
    function row(overrides) {
        const result = {activityId: "tx:single", txid: "single", requestId: "", label: "To Bob",
            address: "bc1qmjp8s0j6mdvkcfjhaauzaapcw09rp0xpqd75u", amount: "0.00075800 BTC",
            netAmountSat: -75800, timestamp: new Date(2026, 8, 14, 22, 3).getTime() / 1000,
            activityType: TransactionActivityModel.Send, status: Transaction.Confirmed, depth: 12,
            blocksToMaturity: 0, statusKnown: true, isInactive: false, isPending: false,
            isPendingRequest: false, replacedByTxid: "", hasPaymentRequest: false,
            actions: [], paymentRequests: [], type: Transaction.SendToAddress, canBump: false}
        for (const key in overrides) result[key] = overrides[key]
        return result
    }
    function rows() {
        return [
            row({activityId: "tx:receive", txid: "receive", label: "Rent · September", amount: "+0.01800000 BTC",
                netAmountSat: 1800000, activityType: TransactionActivityModel.Receive, type: Transaction.RecvWithAddress,
                depth: 2, status: Transaction.Confirming, isPending: false, hasPaymentRequest: true,
                paymentRequests: [{requestId: "paid", label: "Public name", noteSelf: "Rent · September"}]}),
            row({activityId: "request:invoice", txid: "", requestId: "invoice", label: "Friday pizza night",
                isPendingRequest: true, isPending: true, amount: "0.00010000 BTC", netAmountSat: 10000,
                timestamp: new Date(2026, 8, 11, 9, 12).getTime() / 1000,
                activityType: TransactionActivityModel.Receive, statusKnown: false}),
            row({}),
            row({activityId: "tx:batch", txid: "batch", label: "", address: "", amount: "0.00101000 BTC",
                netAmountSat: -101000, activityType: TransactionActivityModel.Multiple, type: Transaction.Other,
                timestamp: new Date(2026, 8, 12, 14, 32).getTime() / 1000,
                actions: [action("robert", "Robert", "0.00060000 BTC", TransactionActivityModel.SendAction),
                    action("elisabeth", "Elisabeth", "0.00040000 BTC", TransactionActivityModel.SendAction)]}),
            row({activityId: "tx:consolidation", txid: "consolidation", label: "", amount: "0.00000800 BTC",
                activityType: TransactionActivityModel.Consolidation, netAmountSat: -800,
                timestamp: new Date(2026, 7, 28, 20, 16).getTime() / 1000}),
            row({activityId: "tx:split", txid: "split", label: "", address: "", amount: "0.00001000 BTC",
                activityType: TransactionActivityModel.Split, netAmountSat: -1000,
                timestamp: new Date(2026, 7, 22, 15, 20).getTime() / 1000,
                actions: [action("savings", "Savings", "0.00150000 BTC", TransactionActivityModel.InternalAction),
                    action("travel", "Travel", "0.00100000 BTC", TransactionActivityModel.InternalAction)]}),
            row({activityId: "tx:mixed", txid: "mixed", label: "", address: "", amount: "0.00025800 BTC",
                activityType: TransactionActivityModel.Multiple, netAmountSat: -25800,
                timestamp: new Date(2026, 7, 12, 22, 3).getTime() / 1000,
                actions: [Object.assign(action("contribution", "", "0.00100000 BTC", TransactionActivityModel.SendAction),
                    {source: TransactionActivityModel.WalletInputs, address: "", outputIndex: -1}),
                    action("receipt", "Personal wallet", "+0.00074200 BTC", TransactionActivityModel.ReceiveAction)]}),
            row({activityId: "tx:mined", txid: "mined", label: "Mining reward", amount: "+3.12500000 BTC",
                activityType: TransactionActivityModel.Mined, netAmountSat: 312500000,
                timestamp: new Date(2026, 6, 20, 20, 16).getTime() / 1000}),
            row({activityId: "tx:cancelled", txid: "cancelled", label: "To Noah", amount: "0.00020000 BTC",
                isInactive: true, status: Transaction.Abandoned, depth: 0,
                timestamp: new Date(2026, 6, 12, 17, 40).getTime() / 1000})
        ]
    }
    function init() {
        Theme.dark = true
        walletController.reset()
        walletController.initialized = true
        walletController.setSelectedWalletObject(testWalletModel)
        nodeModel.setBlockSyncActiveForTest(false)
        testTransactionActivityModel.loading = false
        testTransactionActivityModel.loadError = ""
        testTransactionActivityModel.setRows(rows())
        testWalletModel.lastLoadedPaymentRequestDetailId = ""
        testWalletModel.lastRemovedRequestId = ""
        testWalletModel.removeReceiveRequestResult = true
    }
    function createPage(properties) {
        const page = createTemporaryObject(pageComponent, this, properties || {})
        verify(page !== null)
        waitForRendering(page)
        return page
    }
    function findRow(page, txid) {
        const list = findChild(page, "activityListView")
        let result = findChild(page, "activityItem_" + txid)
        for (let i = 0; !result && i < list.count; ++i) {
            list.positionViewAtIndex(i, ListView.Beginning)
            waitForRendering(list)
            result = findChild(page, "activityItem_" + txid)
        }
        verify(result !== null, "Transaction row " + txid)
        return result
    }
    function openMore(page) {
        mouseClick(findChild(page, "activityMoreButton"))
        tryCompare(findChild(page, "activityMoreMenu"), "opened", true)
    }
    function chooseGrouping(page, name) {
        openMore(page)
        const menu = findChild(page, "activityMoreMenu")
        mouseClick(findChild(menu.contentItem, name))
        tryCompare(menu, "visible", false)
    }

    function test_right_click_copy_menu_data() {
        return [
            {tag: "transaction", id: "single", child: false, request: false},
            {tag: "batch_child", id: "batch", child: true, request: false},
            {tag: "payment_request", id: "invoice", child: false, request: true}
        ]
    }
    function test_right_click_copy_menu(data) {
        const entries = rows()
        for (const entry of entries) {
            entry.rawTransaction = "02000000abcdef"
            entry.uri = "bitcoin:bc1qexample?amount=0.0001&label=Invoice"
        }
        testTransactionActivityModel.setRows(entries)
        const page = createPage()
        const item = data.request ? findChild(page, "activityRequest_invoice") : findRow(page, data.id)
        verify(item !== null)
        const target = data.child ? findChild(item, "activityAction_robert") : item
        mouseClick(target, target.width / 2, target.height / 2, Qt.RightButton)
        const menu = findChild(page, "activityRowContextMenu")
        tryCompare(menu, "opened", true)
        compare(page.depth, 1)
        const copyId = findChild(menu.contentItem, "activityCopyTransactionId")
        const copyRaw = findChild(menu.contentItem, "activityCopyRawTransaction")
        const copyRequest = findChild(menu.contentItem, "activityCopyPaymentRequest")
        compare(copyId.visible, !data.request)
        compare(copyRaw.visible, !data.request)
        compare(copyRequest.visible, data.request)
        compare(findChild(menu.contentItem, "activityDeletePaymentRequest").visible, data.request)
        compare(findChild(menu.contentItem, "activityDeletePaymentRequestDivider").visible, data.request)
        if (data.request) {
            mouseClick(copyRequest)
            compare(Clipboard.text(), "bitcoin:bc1qexample?amount=0.0001&label=Invoice")
            compare(testWalletModel.lastLoadedPaymentRequestDetailId, "")
        } else {
            mouseClick(copyId)
            compare(Clipboard.text(), data.id)
            tryCompare(menu, "visible", false)
            mouseClick(target, target.width / 2, target.height / 2, Qt.RightButton)
            tryCompare(menu, "opened", true)
            mouseClick(copyRaw)
            compare(Clipboard.text(), "02000000abcdef")
        }
        tryCompare(menu, "visible", false)
        compare(page.depth, 1)
    }

    function test_right_click_delete_request_data() {
        return [{tag: "success", succeeds: true}, {tag: "failure", succeeds: false}]
    }
    function test_right_click_delete_request(data) {
        testWalletModel.removeReceiveRequestResult = data.succeeds
        const page = createPage()
        const item = findChild(page, "activityRequest_invoice")
        mouseClick(item, item.width / 2, item.height / 2, Qt.RightButton)
        const menu = findChild(page, "activityRowContextMenu")
        tryCompare(menu, "opened", true)
        const button = findChild(menu.contentItem, "activityDeletePaymentRequest")
        compare(button.role, ContextMenuButton.Destructive)
        mouseClick(button)
        if (data.succeeds) {
            tryCompare(menu, "visible", false)
            compare(testWalletModel.lastRemovedRequestId, "invoice")
        } else {
            verify(menu.opened)
            compare(testWalletModel.lastRemovedRequestId, "")
            verify(findChild(menu.contentItem, "activityDeletePaymentRequestError").visible)
        }
        compare(page.depth, 1)
    }

    function test_rows_share_metadata_and_keep_batch_children_visible() {
        const page = createPage()
        const single = findRow(page, "single")
        const batch = findRow(page, "batch")
        compare(findChild(single, "activityRowAmount").text, "0.00075800 BTC")
        compare(findChild(batch, "activityRowAmount").text, "0.00101000 BTC")
        verify(!findChild(single, "activityRowMetadata").text.includes("Confirmed"))
        verify(!findChild(batch, "activityRowMetadata").text.includes("Confirmed"))
        compare(findChild(batch, "activityRowAddress").visible, false)
        verify(findChild(batch, "activityAction_robert") !== null)
        verify(findChild(batch, "activityAction_elisabeth") !== null)
        const icon = findChild(batch, "activityRowIcon")
        compare(icon.iconSource.toString(), "qrc:/icons/activity-send.svg")
        compare(icon.accent, Theme.color.orange)
        const children = findChild(batch, "activityRowChildren")
        verify(children.height >= findChild(batch, "activityAction_robert").height
            + findChild(batch, "activityAction_elisabeth").height)
        compare(findChild(children, "activityRowMetadata"), null)
        compare(findChild(children, "activityTransactionDivider"), null)

        // A grouped transaction's icon follows its net wallet impact.
        const updated = rows()
        updated[3].netAmountSat = 101000
        updated[3].amount = "+0.00101000 BTC"
        testTransactionActivityModel.setRows(updated)
        const incomingIcon = findChild(findRow(page, "batch"), "activityRowIcon")
        compare(incomingIcon.iconSource.toString(), "qrc:/icons/activity-receive.svg")
        compare(incomingIcon.accent, Theme.color.green)
    }

    function test_whole_row_highlights_and_opens_transaction_data() {
        return [
            {tag: "note", txid: "single", target: "activityRowLabel"},
            {tag: "address", txid: "single", target: "activityRowAddress"},
            {tag: "amount", txid: "single", target: "activityRowAmount"},
            {tag: "empty_space", txid: "single", target: ""},
            {tag: "child_note", txid: "batch", target: "activityActionLabel"},
            {tag: "child_address", txid: "batch", target: "activityActionAddress"},
            {tag: "child_amount", txid: "batch", target: "activityActionAmount"},
            {tag: "group_padding", txid: "batch", target: ""}
        ]
    }

    function test_whole_row_highlights_and_opens_transaction(data) {
        const page = createPage()
        findChild(page, "activitySearchField").text = data.txid === "batch" ? "Robert" : "To Bob"
        tryCompare(findChild(page, "activityListView"), "count", 1)
        const row = findRow(page, data.txid)
        const target = data.target.length > 0 ? findChild(row, data.target) : row
        verify(target !== null)
        let copiedText = "unchanged"
        const clipboard = { setText: function(value) { copiedText = value } }
        if (data.target.endsWith("Address")) {
            target.clipboard = clipboard
            verify(target.isTruncated)
            verify(target.displayAddress.includes("…"))
            compare(target.interactive, false)
        }
        waitForRendering(page)
        const x = target.width / 2
        const y = data.target.length > 0 ? target.height / 2 : row.height - 3
        mouseMove(target, x, y)
        tryCompare(findChild(row, "activityRowBackground"), "color", Theme.color.neutral2)
        compare(findChild(row, "activityRowLabel").color, Theme.color.neutral9)

        mouseClick(target, x, y)
        tryCompare(page, "depth", 2)
        compare(page.currentItem.txid, data.txid)
        compare(page.currentItem.outputIndex, -1)
        compare(copiedText, "unchanged")
    }

    function test_whole_row_supports_keyboard_activation() {
        const page = createPage()
        findChild(page, "activitySearchField").text = "Robert"
        tryCompare(findChild(page, "activityListView"), "count", 1)
        const button = findChild(findRow(page, "batch"), "activityRowOpenButton")
        waitForRendering(page)
        button.forceActiveFocus()
        tryCompare(button, "activeFocus", true)
        keyClick(Qt.Key_Space)
        tryCompare(page, "depth", 2)
        compare(page.currentItem.txid, "batch")
    }

    function test_request_amount_visibility_data() {
        return [
            {tag: "unspecified", request: true, sat: 0, shown: false},
            {tag: "specified", request: true, sat: 10000, shown: true},
            {tag: "zero_transaction", request: false, sat: 0, shown: true}
        ]
    }
    function test_request_amount_visibility(data) {
        testTransactionActivityModel.setRows([row({
            activityId: data.request ? "request:amount" : "tx:amount", txid: data.request ? "" : "amount",
            requestId: data.request ? "amount" : "", isPendingRequest: data.request,
            isPending: data.request, netAmountSat: data.sat,
            amount: data.sat === 0 ? "0.00000000 BTC" : "0.00010000 BTC"
        })])
        const page = createPage()
        const item = findChild(page, data.request ? "activityRequest_amount" : "activityItem_amount")
        verify(item !== null)
        compare(findChild(item, "activityRowAmount").visible, data.shown)
        compare(findChild(item, "activityRowCompactAmount").visible, false)
        page.width = 390
        tryCompare(item, "compact", true)
        compare(findChild(item, "activityRowAmount").visible, false)
        compare(findChild(item, "activityRowCompactAmount").visible, data.shown)
    }

    function test_pending_request_and_confirmation_presentation() {
        const page = createPage()
        for (const depth of [0, 1, 2, 3, 4, 5]) {
            const fixture = rows()
            fixture[0].depth = depth
            fixture[0].status = depth === 0 ? Transaction.Unconfirmed : Transaction.Confirming
            fixture[0].isPending = depth === 0
            testTransactionActivityModel.setRows(fixture)
            const receive = findRow(page, "receive")
            const metadata = findChild(receive, "activityRowMetadata").text
            verify(metadata.endsWith(depth === 0 ? "Unconfirmed" : depth === 1 ? "1 confirmation" : depth + " confirmations"))
            verify(!metadata.includes("0 confirmations"))
            compare(findChild(receive, "activityPendingRing").visible, depth === 0)
        }
        const receive = findRow(page, "receive")
        verify(findChild(receive, "activityRowMetadata").text.endsWith("5 confirmations"))
        compare(findChild(receive, "activityRowIcon").iconSource.toString(), "qrc:/icons/activity-receive.svg")
        verify(!findChild(receive, "activityPendingRing").visible)
        compare(findChild(receive, "activityRowAmount").color, Theme.color.green)
        verify(findChild(receive, "activityRowRequestBadge").visible)
        const request = findChild(page, "activityRequest_invoice")
        verify(request !== null)
        verify(findChild(request, "activityRowMetadata").text.includes("Payment request · Awaiting payment"))
        compare(findChild(request, "activityRowAmount").color, Theme.color.neutral7)
        compare(findChild(request, "activityRowIcon").iconSource.toString(), "qrc:/icons/activity-payment-request.svg")
        compare(findChild(request, "activityRowIcon").accent, Theme.color.lavender)
        verify(findChild(request, "activityPendingRing").visible)
        mouseClick(findChild(request, "activityRowOpenButton"))
        tryCompare(findChild(page, "paymentRequestModal"), "opened", true)
        compare(page.depth, 1)
        compare(testWalletModel.lastLoadedPaymentRequestDetailId, "invoice")
    }

    function test_pending_balance_excludes_requests_and_updates_with_transactions() {
        const fixture = rows()
        fixture[0].netAmountSat = 111000
        for (const i of [0, 3]) {
            fixture[i].depth = 0
            fixture[i].status = Transaction.Unconfirmed
            fixture[i].isPending = true
        }
        testTransactionActivityModel.setRows(fixture)
        const page = createPage()
        const proxy = findChild(page, "activityFilterProxyModel")
        proxy.displayUnit = BitcoinAmount.SAT
        let total = findChild(page, "activityPendingBalance")
        verify(total !== null)
        compare(proxy.pendingBalanceSat, 10000)
        compare(total.text, "+10000 sats")
        compare(total.color, Theme.color.green)
        verify(waitForRendering(page))
        fuzzyCompare(total.mapToItem(page, total.width, 0).x, page.width - page.currentItem.activitySideInset, 0.1)
        compare(findChild(findChild(page, "activityRequest_invoice"), "activityRowAmount").color, Theme.color.neutral7)

        findChild(page, "activitySearchField").text = "Robert"
        tryCompare(proxy, "pendingBalanceSat", -101000)
        total = findChild(page, "activityPendingBalance")
        compare(total.text, "-101000 sats")
        tryCompare(total, "color", Theme.color.neutral9)
        findChild(page, "activitySearchField").text = ""
        tryCompare(proxy, "pendingBalanceSat", 10000)
        fixture[3].depth = 1
        fixture[3].isPending = false
        testTransactionActivityModel.setRows(fixture)
        tryCompare(proxy, "pendingBalanceSat", 111000)
        fixture[0].depth = 1
        fixture[0].isPending = false
        testTransactionActivityModel.setRows(fixture)
        tryCompare(proxy, "pendingBalanceSat", 0)
        compare(findChild(page, "activityPendingBalance").visible, false)
        page.width = 390
        verify(waitForRendering(page))
        tryCompare(findChild(findChild(page, "activityRequest_invoice"), "activityRowCompactAmount"), "color", Theme.color.neutral7)
    }

    function test_child_search_keeps_whole_transaction_and_clear_restores_rows() {
        const page = createPage()
        const search = findChild(page, "activitySearchField")
        search.text = "Elisabeth"
        const list = findChild(page, "activityListView")
        tryCompare(list, "count", 1)
        const batch = findRow(page, "batch")
        verify(findChild(batch, "activityAction_robert") !== null)
        waitForRendering(page)
        mouseClick(findChild(page, "activityClearSearchButton"))
        tryCompare(list, "count", rows().length)
        compare(search.text, "")
    }

    function test_display_density_hides_children_and_is_saved() {
        const samples = rows()
        testTransactionActivityModel.setRows([samples[2], samples[3], row({
            txid: "no-note", activityId: "tx:no-note", label: ""
        })])
        let page = createPage()
        const single = findRow(page, "single")
        const batch = findRow(page, "batch")
        compare(single.isCompact, false)
        const comfortableHeight = single.height
        const comfortableBatchHeight = batch.height
        const metadata = single.metadata
        const labelSize = findChild(single, "activityRowLabel").font.pixelSize
        openMore(page)
        const menu = findChild(page, "activityMoreMenu")
        compare(findChild(menu.contentItem, "activityDisplayPicker").currentValue, "comfortable")
        mouseClick(findChild(menu.contentItem, "activityDisplayCompact"))
        tryCompare(menu, "visible", false)
        tryCompare(single, "isCompact", true)
        tryVerify(function() { return single.height < comfortableHeight && batch.height < comfortableBatchHeight })
        compare(findChild(single, "activityRowIcon").width, 32)
        verify(findChild(single, "activityRowLabel").visible)
        verify(!findChild(single, "activityRowAddress").visible)
        verify(findChild(findRow(page, "no-note"), "activityRowAddress").visible)
        compare(findChild(single, "activityRowLabel").font.pixelSize, labelSize)
        compare(single.metadata, metadata)
        verify(batch.metadata.endsWith("2 recipients"))
        tryCompare(findChild(batch, "activityRowChildren"), "height", 0)
        compare(findChild(batch, "activityAction_robert"), null)
        page.width = 390
        tryCompare(single, "compact", true)
        verify(single.isCompact)
        verify(!findChild(single, "activityRowAddress").visible)
        page.destroy()
        wait(0)
        page = createPage()
        compare(findRow(page, "single").isCompact, true)
        chooseGrouping(page, "activityDisplayComfortable")
        const restored = findRow(page, "single")
        tryCompare(restored, "isCompact", false)
        verify(findChild(restored, "activityRowAddress").visible)
        tryVerify(function() { return findChild(findRow(page, "batch"), "activityAction_robert") !== null })
    }

    function test_grouping_menu_updates_dates_and_is_saved() {
        const page = createPage()
        chooseGrouping(page, "activityGroupDay")
        const proxy = findChild(page, "activityFilterProxyModel")
        tryCompare(proxy, "groupBy", ActivityFilterProxyModel.Day)
        const single = findRow(page, "single")
        compare(single.dateTimeLabel, "10:03 PM")
        page.destroy()
        wait(0)
        const other = createPage()
        compare(findChild(other, "activityFilterProxyModel").groupBy, ActivityFilterProxyModel.Day)
        chooseGrouping(other, "activityGroupMonth")
        tryCompare(findChild(other, "activityFilterProxyModel"), "groupBy", ActivityFilterProxyModel.Month)
        compare(findRow(other, "single").dateTimeLabel, "Sep 14, 10:03 PM")
    }

    function test_parent_navigation_uses_wallet_total_and_updates_live() {
        const page = createPage()
        const batch = findRow(page, "batch")
        mouseClick(findChild(batch, "activityRowOpenButton"))
        tryCompare(page, "depth", 2)
        compare(page.currentItem.amount, "0.00101000 BTC")
        compare(page.currentItem.txid, "batch")
        compare(page.currentItem.outputIndex, -1)
        compare(page.currentItem.transactionData.label, "") // GUI group headings are never notes.
        const updated = rows()
        updated[3].depth = 20
        testTransactionActivityModel.setRows(updated)
        tryCompare(page.currentItem, "depth", 20)
        page.navigateToTransaction("batch", 0)
        tryCompare(page, "depth", 2)
        compare(page.currentItem.amount, "0.00101000 BTC") // The hero always represents the wallet's net impact.
        compare(page.currentItem.outputIndex, 0)
    }

    function test_details_open_while_loading_and_refresh_when_ready() {
        const fixture = rows()
        fixture[3].detailsLoading = true
        testTransactionActivityModel.setRows(fixture)
        const page = createPage()
        page.navigateToTransaction("batch")
        tryCompare(page, "depth", 2)
        verify(page.currentItem.detailsLoading)
        verify(findChild(page.currentItem, "transactionDetailsLoading").visible)
        fixture[3].detailsLoading = false
        fixture[3].detailsError = "Wallet activity could not be loaded. Please try again."
        testTransactionActivityModel.setRows(fixture)
        tryCompare(page.currentItem, "detailsLoading", false)
        verify(findChild(page.currentItem, "transactionDetailsError").visible)
        fixture[3].detailsError = ""
        fixture[3].depth = 3
        testTransactionActivityModel.setRows(fixture)
        tryCompare(page.currentItem, "depth", 3)
        verify(!findChild(page.currentItem, "transactionDetailsError").visible)
    }

    function test_request_modal_preserves_activity_and_transaction_navigation() {
        const page = createPage()
        page.currentItem.paymentRequestRequested("invoice")
        const modal = findChild(page, "paymentRequestModal")
        verify(modal !== null)
        tryCompare(modal, "opened", true)
        compare(page.depth, 1)
        compare(page.currentItem.objectName, "activityPage")
        modal.close()
        tryCompare(modal, "visible", false)
        page.navigateToTransaction("batch")
        tryCompare(page.currentItem, "txid", "batch")
        const detail = page.currentItem
        detail.openPaymentRequestDetail("invoice")
        const detailModal = findChild(detail, "paymentRequestModal")
        tryCompare(detailModal, "opened", true)
        compare(page.depth, 2)
        compare(page.currentItem, detail)
        detailModal.close()
        tryCompare(detailModal, "visible", false)
        compare(page.currentItem.txid, "batch")
        compare(testTransactionActivityModel.requestedTransaction(), "batch")
        page.pop()
        tryCompare(page, "depth", 1)
    }

    function test_history_loading_and_failure_states() {
        testTransactionActivityModel.setRows([])
        testTransactionActivityModel.loading = true
        const page = createPage()
        compare(findChild(page, "activityEmptyStateTitle").text, "Loading wallet activity…")
        testTransactionActivityModel.loading = false
        testTransactionActivityModel.loadError = "Please try again."
        tryCompare(findChild(page, "activityEmptyStateTitle"), "text", "Activity could not be loaded")
        verify(findChild(page, "activityRetryButton").visible)
    }

    function test_amount_filter_and_empty_states() {
        const page = createPage()
        const proxy = findChild(page, "activityFilterProxyModel")
        mouseClick(findChild(page, "activityAmountFilterButton"))
        tryCompare(findChild(page, "activityAmountFilterPopup"), "opened", true)
        const slider = findChild(page, "activityAmountRangeSlider")
        compare(slider.minValue, 0)
        compare(slider.maxValue, 312500000)
        slider.setValues(50000000, 200000000)
        page.currentItem.applyAmountRange()
        compare(proxy.minAmount, 50000000)
        compare(proxy.maxAmount, 200000000)
        compare(findChild(page, "activityAmountFilterButton").text, "Amount")
        verify(findChild(page, "activityAmountFilterButton").active)
        page.currentItem.clearFilters()
        findChild(page, "activitySearchField").text = "no such transaction"
        tryCompare(findChild(page, "activityEmptyStateTitle"), "text", "No activity matches your filters.")
        testTransactionActivityModel.setRows([])
        nodeModel.setBlockSyncActiveForTest(true)
        tryCompare(findChild(page, "activityEmptyStateTitle"), "text", "Syncing wallet activity…")
    }

    function test_self_send_uses_right_arrow_and_preserves_note() {
        const fixture = row({activityId: "tx:self", txid: "self", label: "", netAmountSat: -1000,
            activityType: TransactionActivityModel.InternalTransfer, type: Transaction.SendToSelf,
            actions: [action("self", "", "0.00060000 BTC", TransactionActivityModel.InternalAction)]})
        testTransactionActivityModel.setRows([fixture])
        const page = createPage()
        const selfSend = findRow(page, "self")
        compare(selfSend.displayLabel, "Sent to yourself")
        compare(selfSend.hasChildren, false)
        compare(findChild(selfSend, "activityRowIcon").iconSource.toString(), "qrc:/icons/activity-internal.svg")
        compare(findChild(selfSend, "activityRowIcon").accent, Theme.color.purple)
        const purple = Theme.color.purple
        compare(findChild(selfSend, "activityRowIcon").color, Qt.rgba(purple.r, purple.g, purple.b, 64 / 255))
        fixture.label = "Personal savings"
        testTransactionActivityModel.setRows([fixture])
        compare(findRow(page, "self").displayLabel, "Personal savings")
    }

    function test_sent_to_yourself_filter_includes_all_internal_categories() {
        const fixture = rows()
        fixture.push(row({activityId: "tx:self", txid: "self", label: "", netAmountSat: -1000,
            activityType: TransactionActivityModel.InternalTransfer, type: Transaction.SendToSelf,
            actions: [action("self", "", "0.00060000 BTC", TransactionActivityModel.InternalAction)]}))
        testTransactionActivityModel.setRows(fixture)
        const page = createPage()
        mouseClick(findChild(page, "activityTypeFilterButton"))
        const menu = findChild(page, "activityTypeFilterPopup")
        tryCompare(menu, "opened", true)
        compare(findChild(menu.contentItem, "activityTypeConsolidation"), null)
        compare(findChild(menu.contentItem, "activityTypeSplit"), null)
        const sentToSelf = findChild(menu.contentItem, "activityTypeSentToSelf")
        compare(sentToSelf.text, "Sent to yourself")
        mouseClick(sentToSelf)
        tryCompare(findChild(page, "activityListView"), "count", 3)
        for (const txid of ["self", "consolidation", "split"])
            verify(findRow(page, txid) !== null)
    }

    function test_special_types_and_compact_layout() {
        const page = createPage({width: 390, height: 900})
        const batch = findRow(page, "batch")
        verify(batch.compact)
        verify(findChild(batch, "activityRowCompactAmount").visible)
        verify(!findChild(batch, "activityRowAmount").visible)
        const split = findRow(page, "split")
        compare(findChild(split, "activityRowIcon").iconSource.toString(), "qrc:/icons/activity-split.svg")
        const mixed = findRow(page, "mixed")
        compare(findChild(findChild(mixed, "activityAction_contribution"), "activityActionLabel").visible, false)
        const mined = findRow(page, "mined")
        compare(findChild(mined, "activityRowIcon").iconSource.toString(), "qrc:/icons/coinbase.svg")
        const cancelled = findRow(page, "cancelled")
        verify(cancelled.metadata.endsWith("Cancelled"))
        compare(cancelled.amountColor, Theme.color.neutral6)
    }

    function test_missing_notes_hide_label_lines_but_preserve_request_badges() {
        const fixture = rows()
        fixture[0].label = ""
        fixture[1].label = "   "
        fixture[2].label = ""
        fixture[6].actions[1].label = ""
        fixture[6].actions[1].hasPaymentRequest = true
        testTransactionActivityModel.setRows(fixture)
        const page = createPage()
        const receive = findRow(page, "receive")
        compare(findChild(receive, "activityRowLabel").visible, false)
        const badge = findChild(receive, "activityRowRequestBadge")
        verify(badge.visible)
        compare(badge.parent, findChild(receive, "activityRowAddress").parent)
        verify(badge.x >= findChild(receive, "activityRowAddress").width + 8)
        compare(findChild(findChild(page, "activityRequest_invoice"), "activityRowLabel").visible, false)
        compare(findChild(findRow(page, "single"), "activityRowLabel").visible, false)
        const batch = findRow(page, "batch")
        compare(batch.displayLabel, "Multiple actions")
        compare(batch.label, "")
        compare(findRow(page, "consolidation").displayLabel, "Consolidation")
        compare(findRow(page, "split").displayLabel, "Split")
        const mixed = findRow(page, "mixed")
        const receipt = findChild(mixed, "activityAction_receipt")
        compare(findChild(receipt, "activityActionLabel").visible, false)
        verify(findChild(receipt, "activityActionRequestBadge").visible)
        compare(findChild(receipt, "activityActionRequestBadge").parent,
            findChild(receipt, "activityActionAddress").parent)
        verify(findChild(receipt, "activityActionRequestBadge").x >= findChild(receipt, "activityActionAddress").width + 8)
        compare(findChild(page, "activityListView").footer, null)

        // Live edits restore/remove the note line without losing the badge.
        fixture[0].label = "User-entered rent note"
        testTransactionActivityModel.setRows(fixture)
        const edited = findRow(page, "receive")
        verify(findChild(edited, "activityRowLabel").visible)
        compare(findChild(edited, "activityRowRequestBadge").parent,
            findChild(edited, "activityRowLabel").parent)
        verify(findChild(edited, "activityRowRequestBadge").x >= findChild(edited, "activityRowLabel").width + 8)
    }

    function test_filter_menus_export_and_wallet_switch() {
        const page = createPage()
        const proxy = findChild(page, "activityFilterProxyModel")
        mouseClick(findChild(page, "activityTypeFilterButton"))
        const types = findChild(page, "activityTypeFilterPopup")
        tryCompare(types, "opened", true)
        mouseClick(findChild(types.contentItem, "activityTypeMultiple"))
        compare(proxy.typeFilters, [ActivityFilterProxyModel.Multiple])
        compare(types.opened, true)
        types.close()
        tryCompare(types, "visible", false)

        mouseClick(findChild(page, "activityDateFilterButton"))
        const dates = findChild(page, "activityDateFilterPopup")
        tryCompare(dates, "opened", true)
        mouseClick(findChild(dates.contentItem, "activityDateThisMonth"))
        compare(proxy.dateFilter, ActivityFilterProxyModel.ThisMonth)
        tryCompare(dates, "visible", false)
        page.currentItem.clearFilters()

        findChild(page, "activityExportPathField").text = "/tmp/activity-list-test.csv"
        openMore(page)
        mouseClick(findChild(findChild(page, "activityMoreMenu").contentItem, "activityExportButton"))
        const result = findChild(page, "activityExportResultPopup")
        tryCompare(result, "opened", true)
        compare(findChild(result.contentItem, "activityExportResultTitle").text, "Export complete")
        result.close()
        tryCompare(result, "visible", false)

        proxy.searchText = "Robert"
        page.navigateToTransaction("batch")
        tryCompare(page, "depth", 2)
        walletController.setSelectedWallet("another-wallet")
        tryCompare(page, "depth", 1)
    }

    function test_filter_button_animates_and_reserves_space() {
        const page = createPage()
        const proxy = findChild(page, "activityFilterProxyModel")
        const button = findChild(page, "activityTypeFilterButton")
        const badge = findChild(page, "activityActiveFiltersButton")
        const initialX = button.x
        proxy.typeFilters = [ActivityFilterProxyModel.Sent]
        tryVerify(function() { return badge.opacity > 0 && badge.opacity < 1 })
        verify(badge.scale < 1)
        tryCompare(badge, "opacity", 1)
        compare(badge.scale, 1)
        verify(button.x > initialX)
        const expandedX = button.x
        proxy.typeFilters = []
        tryVerify(function() { return badge.opacity > 0 && badge.opacity < 1 })
        verify(badge.visible)
        compare(badge.count, 1)
        tryVerify(function() { return button.x < expandedX })
        tryCompare(badge, "visible", false)
        tryCompare(button, "x", initialX)
    }

    function test_multiselect_types_and_counted_clear_menu() {
        const page = createPage()
        const proxy = findChild(page, "activityFilterProxyModel")
        const button = findChild(page, "activityTypeFilterButton")
        const badge = findChild(page, "activityActiveFiltersButton")
        compare(button.text, "Activity")
        compare(findChild(page, "activityDateFilterButton").text, "Date")
        compare(findChild(page, "activityAmountFilterButton").text, "Amount")
        tryCompare(badge, "visible", false)
        mouseClick(button)
        const menu = findChild(page, "activityTypeFilterPopup")
        tryCompare(menu, "opened", true)
        const all = findChild(menu.contentItem, "activityTypeAll")
        const sent = findChild(menu.contentItem, "activityTypeSent")
        const sentToSelf = findChild(menu.contentItem, "activityTypeSentToSelf")
        verify(all.selected)
        compare(all.text, "All activity")
        mouseClick(sent)
        mouseClick(sentToSelf)
        compare(menu.opened, true)
        verify(sent.selected && sentToSelf.selected && !all.selected)
        compare(proxy.typeFilters.length, 2)
        compare(badge.count, 1)
        compare(button.background.color, Theme.color.orange)
        compare(findChild(button, "dropdownButtonCaret").color, Theme.color.neutral9)
        compare(badge.background.color, Theme.color.orange)
        verify(findRow(page, "batch") !== null)
        verify(findRow(page, "consolidation") !== null)
        verify(findRow(page, "split") !== null)
        mouseClick(sent)
        mouseClick(sentToSelf)
        verify(all.selected)
        compare(button.active, false)
        tryCompare(badge, "visible", false)
        mouseClick(sent)
        mouseClick(sentToSelf)
        mouseClick(all)
        compare(proxy.typeFilters.length, 0)
        verify(!sent.selected && !sentToSelf.selected)
        mouseClick(sent)
        menu.close()
        tryCompare(menu, "visible", false)
        proxy.dateFilter = ActivityFilterProxyModel.ThisYear
        proxy.setAmountRange(0, 200000)
        compare(badge.count, 3)
        findChild(page, "activitySearchField").text = "Robert"
        compare(badge.count, 3) // Search is independent of the three filter categories.
        waitForRendering(page)
        mouseClick(badge)
        const clearMenu = findChild(page, "activityClearFiltersMenu")
        tryCompare(clearMenu, "opened", true)
        const clear = findChild(clearMenu.contentItem, "activityClearFiltersAction")
        compare(clear.role, ContextMenuButton.Destructive)
        mouseClick(clear)
        tryCompare(badge, "visible", false)
        compare(proxy.typeFilters.length, 0)
        compare(proxy.dateFilter, ActivityFilterProxyModel.DateAll)
        compare(proxy.minAmount, -1)
        compare(proxy.maxAmount, -1)
        compare(proxy.searchText, "Robert")
    }

    function test_date_presets_are_single_selection_and_custom_range_activates() {
        const page = createPage()
        const proxy = findChild(page, "activityFilterProxyModel")
        const button = findChild(page, "activityDateFilterButton")
        mouseClick(button)
        const menu = findChild(page, "activityDateFilterPopup")
        tryCompare(menu, "opened", true)
        compare(findChild(menu.contentItem, "activityDateAll"), null)
        mouseClick(findChild(menu.contentItem, "activityDateToday"))
        tryCompare(menu, "visible", false)
        verify(button.active)
        mouseClick(button)
        tryCompare(menu, "opened", true)
        verify(findChild(menu.contentItem, "activityDateToday").selected)
        mouseClick(findChild(menu.contentItem, "activityDateThisYear"))
        tryCompare(menu, "visible", false)
        mouseClick(button)
        tryCompare(menu, "opened", true)
        verify(!findChild(menu.contentItem, "activityDateToday").selected)
        verify(findChild(menu.contentItem, "activityDateThisYear").selected)
        mouseClick(findChild(menu.contentItem, "activityDateCustomRange"))
        const calendar = findChild(page, "activityCalendar")
        calendar.seed(new Date(2026, 8, 1), new Date(2026, 8, 15))
        mouseClick(findChild(page, "activityDateRangeApply"))
        compare(proxy.dateFilter, ActivityFilterProxyModel.CustomRange)
        verify(button.active)
        compare(button.text, "Date")
    }

    function test_amount_slider_updates_live_and_resets() {
        const page = createPage({width: 390})
        const proxy = findChild(page, "activityFilterProxyModel")
        const button = findChild(page, "activityAmountFilterButton")
        mouseClick(button)
        const menu = findChild(page, "activityAmountFilterPopup")
        tryCompare(menu, "opened", true)
        const slider = findChild(page, "activityAmountRangeSlider")
        const handle = slider.second.handle
        const start = handle.mapToItem(slider, handle.width / 2, handle.height / 2)
        mousePress(slider, start.x, start.y)
        mouseMove(slider, slider.width * 0.6, start.y, 100)
        mouseRelease(slider, slider.width * 0.6, start.y)
        verify(proxy.maxAmount > 0 && proxy.maxAmount < 312500000)
        verify(button.active)
        compare(findChild(page, "activityActiveFiltersButton").count, 1)
        compare(slider.maxValue, 312500000) // Filtering must not shrink the available range.
        const maximum = proxy.maxAmount
        menu.close()
        tryCompare(menu, "visible", false)
        mouseClick(button)
        tryCompare(menu, "opened", true)
        compare(Math.round(slider.upperValue), maximum)
        compare(findChild(page, "activityAmountReset"), null)
        slider.setValues(0, slider.maxValue)
        slider.second.moved()
        compare(button.active, false)
        compare(slider.lowerValue, 0)
        compare(slider.upperValue, slider.maxValue)
        testTransactionActivityModel.setRows([])
        tryCompare(slider, "maxValue", 0)
        compare(slider.enabled, false)
        menu.close()
        testTransactionActivityModel.setRows(rows())
        proxy.setAmountRange(100, 500)
        proxy.typeFilters = [ActivityFilterProxyModel.Sent]
        walletController.setSelectedWalletObject(null)
        tryCompare(proxy, "minAmount", -1)
        compare(proxy.maxAmount, -1)
        compare(proxy.typeFilters.length, 0)
    }

    function test_scrolls_from_page_side_margins_data() {
        return [
            {tag: "desktop_left", pageWidth: 1180, rightSide: false},
            {tag: "desktop_right", pageWidth: 1180, rightSide: true},
            {tag: "compact_left", pageWidth: 390, rightSide: false},
            {tag: "compact_right", pageWidth: 390, rightSide: true}
        ]
    }

    function test_scrolls_from_page_side_margins(data) {
        const page = createPage({width: data.pageWidth, height: 600})
        const list = findChild(page, "activityListView")
        list.positionViewAtBeginning()
        waitForRendering(page)
        verify(list.contentHeight > list.height)
        const row = findRow(page, "receive")
        const inset = row.mapToItem(page, 0, 0).x
        verify(inset > 0)
        const x = data.rightSide ? page.width - inset / 2 : inset / 2
        const y = list.mapToItem(page, 0, list.height / 2).y
        const start = list.contentY
        mouseMove(page, x, y)
        mouseWheel(page, x, y, 0, -120, Qt.NoButton, Qt.NoModifier, 100)
        tryVerify(function() { return list.contentY > start })
        // The margin scrolls the list without becoming a transaction link.
        mouseClick(page, x, y)
        compare(page.depth, 1)
    }

    function test_layout_resizes_and_follows_theme() {
        const page = createPage()
        chooseGrouping(page, "activityGroupMonth")
        const receive = findRow(page, "receive")
        compare(receive.compact, false)
        const list = findChild(page, "activityListView")
        page.width = 390
        list.positionViewAtBeginning()
        tryCompare(receive, "compact", true)
        waitForRendering(page)
        const amount = findChild(receive, "activityRowCompactAmount")
        const metadata = findChild(receive, "activityRowMetadata")
        verify(amount.y >= metadata.y + metadata.height)
        verify(receive.width <= page.width)
        Theme.dark = false
        page.width = 1180
        tryCompare(receive, "compact", false)
        tryCompare(findChild(receive, "activityRowAmount"), "color", Theme.color.green)
        compare(page.currentItem.background.color, Theme.color.neutral0)
        Theme.dark = true
    }
}

// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtTest 1.2
import "../../qml/pages/wallet"

TestCase {
    name: "Activity"
    when: windowShown
    width: 900
    height: 700

    function init() {
        testPaymentRequest.clear()
        testWalletModel.lastLoadedPaymentRequestId = ""
        testWalletModel.lastLoadedPaymentRequestDetailId = ""
        testActivityListModel.setCountForTest(2)
        nodeModel.setBlockSyncActiveForTest(false)
        nodeModel.verificationProgress = 1.0
        walletController.openReceiveRequests = 0
    }

    Component {
        id: activityComponent

        Activity {
            width: 600
            height: 700
        }
    }

    Component {
        id: activityDetailsComponent

        ActivityDetails {
            width: 600
            height: 700
        }
    }

    function detailsProperties(txid, paymentRequests) {
        return {
            txid: txid,
            canBump: false,
            amount: "+0.01000000 BTC",
            date: "2026-01-01",
            depth: 3,
            status: 2,
            type: 1,
            address: "bcrt1qreceiveaddress",
            paymentRequests: paymentRequests || []
        }
    }

    function findActivityEmptyStateText(page, objectName) {
        let text = null
        tryVerify(function() {
            text = findChild(page, objectName)
            return text !== null
        })
        return text
    }

    function test_empty_wallet_while_node_syncing_shows_syncing_copy() {
        testActivityListModel.setCountForTest(0)
        nodeModel.verificationProgress = 0.25
        nodeModel.setBlockSyncActiveForTest(true)

        const page = createTemporaryObject(activityComponent, this)
        verify(page !== null)

        const title = findActivityEmptyStateText(page, "activityEmptyStateTitle")
        const description = findActivityEmptyStateText(page, "activityEmptyStateDescription")
        tryCompare(title, "text", "Syncing wallet activity...")
        compare(description.text, "Transactions may appear as your wallet catches up.")
    }

    function test_empty_wallet_not_syncing_ignores_low_verification_progress() {
        testActivityListModel.setCountForTest(0)
        nodeModel.verificationProgress = 0.25
        nodeModel.setBlockSyncActiveForTest(false)

        const page = createTemporaryObject(activityComponent, this)
        verify(page !== null)

        const title = findActivityEmptyStateText(page, "activityEmptyStateTitle")
        const description = findActivityEmptyStateText(page, "activityEmptyStateDescription")
        tryCompare(title, "text", "No activity yet")
        compare(description.text, "Once you send or receive bitcoin, your transactions will appear here.")
    }

    function test_filtered_empty_copy_stays_separate_from_source_empty_syncing_copy() {
        nodeModel.setBlockSyncActiveForTest(true)

        const page = createTemporaryObject(activityComponent, this)
        verify(page !== null)

        const activityFilterProxy = findChild(page, "activityFilterProxyModel")
        verify(activityFilterProxy !== null)
        activityFilterProxy.searchText = "no matching transaction"

        const title = findActivityEmptyStateText(page, "activityEmptyStateTitle")
        const description = findActivityEmptyStateText(page, "activityEmptyStateDescription")
        tryCompare(title, "text", "No activity matches your filters.")
        compare(description.text, "Try changing your search, date, or type filters.")
    }

    function test_non_empty_activity_still_shows_transaction_rows() {
        nodeModel.verificationProgress = 0.25
        nodeModel.setBlockSyncActiveForTest(true)

        const page = createTemporaryObject(activityComponent, this)
        verify(page !== null)

        tryVerify(function() {
            return findChild(page, "activityItem_aaaa") !== null
        })
        const emptyState = findChild(page, "activityEmptyState")
        if (emptyState !== null) {
            compare(emptyState.visible, false)
        }
    }

    function test_search_toggle_uses_toolbar_icon_size() {
        const page = createTemporaryObject(activityComponent, this)
        verify(page !== null)

        const searchToggle = findChild(page, "activitySearchToggle")
        verify(searchToggle !== null)
        compare(searchToggle.width, 30)
        compare(searchToggle.height, 30)
        compare(searchToggle.iconSize, 24)
    }

    function test_navigateToTransaction_uses_lowest_output_and_supports_exact_output() {
        testActivityListModel.setCountForTest(3)
        const page = createTemporaryObject(activityComponent, this)
        verify(page !== null)

        page.navigateToTransaction("bbbb")
        tryCompare(page, "depth", 2)
        compare(page.currentItem.txid, "bbbb")
        compare(page.currentItem.outputIndex, 1)
        compare(page.currentItem.address, "bcrt1qfirstsendaddress")

        page.pop()
        tryCompare(page, "depth", 1)
        page.navigateToTransaction("bbbb", 2)
        tryCompare(page, "depth", 2)
        compare(page.currentItem.txid, "bbbb")
        compare(page.currentItem.outputIndex, 2)
        compare(page.currentItem.address, "bcrt1qsecondsendaddress")
    }

    function test_selectedWalletChanged_pops_to_root() {
        const page = createTemporaryObject(activityComponent, this)
        verify(page !== null)
        compare(page.depth, 1)

        page.push(activityDetailsComponent, detailsProperties("tx-1"))
        tryCompare(page, "depth", 2)

        page.push(activityDetailsComponent, detailsProperties("tx-2"))
        tryCompare(page, "depth", 3)

        walletController.setSelectedWallet("another-wallet")
        tryCompare(page, "depth", 1)
    }

    function test_editPaymentRequest_preserves_activity_context_until_wallet_switch() {
        const page = createTemporaryObject(activityComponent, this)
        verify(page !== null)
        compare(page.depth, 1)

        page.push(activityDetailsComponent, detailsProperties("tx-1", [
            {
                requestId: "req-1",
                label: "Alice",
                amountDisplay: "0.00100000 BTC",
                date: "Fri Jan 2 2026"
            }
        ]))
        tryCompare(page, "depth", 2)

        const detailsPage = page.currentItem
        verify(detailsPage !== null)

        tryVerify(function() {
            return findChild(detailsPage, "activityDetailsPaymentRequest_0") !== null
        })

        const requestRow = findChild(detailsPage, "activityDetailsPaymentRequest_0")
        verify(requestRow !== null)
        requestRow.clicked()

        tryCompare(page, "depth", 3)
        compare(page.currentItem.objectName, "paymentRequestDetailPage")
        compare(testWalletModel.lastLoadedPaymentRequestDetailId, "req-1")
        compare(testPaymentRequest.id, "req-1")
        compare(testPaymentRequest.isEditing, false)
        const editButton = findChild(page.currentItem, "paymentRequestDetailEdit")
        verify(editButton !== null)
        editButton.clicked()

        compare(testWalletModel.lastLoadedPaymentRequestId, "req-1")
        compare(walletController.openReceiveRequests, 1)
        compare(testPaymentRequest.isEditing, true)
        compare(page.depth, 3)
        compare(page.currentItem.objectName, "paymentRequestDetailPage")

        walletController.setSelectedWallet("third-wallet")
        tryCompare(page, "depth", 1)
    }
}

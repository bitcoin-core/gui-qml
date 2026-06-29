// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtTest 1.2
import org.bitcoincore.qt 1.0
import "../../qml/controls"
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
        id: activityCalendarComponent

        ActivityCalendar {}
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
        compare(description.text, "Try changing your search, date, type, or amount filters.")
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

    function test_minimum_amount_filter_applies_and_clears() {
        const page = createTemporaryObject(activityComponent, this)
        verify(page !== null)

        const proxy = findChild(page, "activityFilterProxyModel")
        const field = findChild(page, "activityMinAmountField")
        const applyButton = findChild(page, "activityMinAmountApply")
        const resetButton = findChild(page, "activityMinAmountReset")
        verify(proxy !== null)
        verify(field !== null)
        verify(applyButton !== null)
        verify(resetButton !== null)

        // Default display unit is BTC, so "0.5" parses to 50,000,000 sat.
        compare(optionsModel.displayUnit, 0)
        field.text = "0.5"
        applyButton.clicked()
        compare(proxy.minAmount, 50000000)

        resetButton.clicked()
        compare(proxy.minAmount, -1)
    }

    function test_minimum_amount_filter_parses_every_display_unit() {
        const page = createTemporaryObject(activityComponent, this)
        verify(page !== null)

        const proxy = findChild(page, "activityFilterProxyModel")
        const field = findChild(page, "activityMinAmountField")
        const applyButton = findChild(page, "activityMinAmountApply")
        verify(proxy !== null)
        verify(field !== null)
        verify(applyButton !== null)

        // The threshold is typed in the active display unit, so it must parse
        // as mBTC and bits too, not only as BTC and sats.
        const cases = [
            {unit: 0, text: "0.5", sats: 50000000},
            {unit: 1, text: "0.5", sats: 50000},
            {unit: 2, text: "0.5", sats: 50},
            {unit: 3, text: "50",  sats: 50},
        ]
        try {
            for (const c of cases) {
                optionsModel.displayUnit = c.unit
                field.text = c.text
                applyButton.clicked()
                compare(proxy.minAmount, c.sats)
            }
        } finally {
            // Leave the shared display unit as found for the other tests.
            optionsModel.displayUnit = 0
        }

        // The field's placeholder and input mask follow the unit as well, so a
        // mBTC or bits threshold is not masked to BTC precision.
        const activityPage = findChild(page, "activityPage")
        verify(activityPage !== null)
        // Units by number: QML cannot name BitcoinAmount.mBTC / uBTC (enum
        // values starting with a lowercase letter are not exposed).
        compare(activityPage.minAmountPlaceholder(0), "0.00000000") // BTC
        compare(activityPage.minAmountPlaceholder(1), "0.00000")    // mBTC
        compare(activityPage.minAmountPlaceholder(2), "0.00")       // uBTC
        compare(activityPage.minAmountPlaceholder(3), "0")          // SAT
        verify(activityPage.minAmountPattern(1).test("1.12345"))
        verify(!activityPage.minAmountPattern(1).test("1.123456"))
        verify(activityPage.minAmountPattern(2).test("1.12"))
        verify(!activityPage.minAmountPattern(2).test("1.123"))
        verify(!activityPage.minAmountPattern(3).test("1.5"))
    }

    function test_custom_date_range_filter_applies() {
        const page = createTemporaryObject(activityComponent, this)
        verify(page !== null)

        const proxy = findChild(page, "activityFilterProxyModel")
        const applyButton = findChild(page, "activityDateRangeApply")
        verify(proxy !== null)
        verify(applyButton !== null)

        // The calendar opens on the current month; pick day 10 (From) and day
        // 20 (To), which exist in every month. Clicking a day delegate fires
        // its clicked() handler the same way a real tap does.
        const now = new Date()
        const fromIso = Qt.formatDate(new Date(now.getFullYear(), now.getMonth(), 10), "yyyy-MM-dd")
        const toIso = Qt.formatDate(new Date(now.getFullYear(), now.getMonth(), 20), "yyyy-MM-dd")
        // Repeater-generated cells are visual children of the grid but not
        // QObject children, so look them up via the grid rather than findChild.
        const grid = findChild(page, "activityCalendarGrid")
        verify(grid !== null)
        function dayCell(iso) {
            for (var i = 0; i < grid.children.length; ++i) {
                if (grid.children[i].objectName === "calendarDay_" + iso) {
                    return grid.children[i]
                }
            }
            return null
        }
        const fromDay = dayCell(fromIso)
        const toDay = dayCell(toIso)
        verify(fromDay !== null)
        verify(toDay !== null)

        fromDay.clicked() // From is active by default, then advances to To
        toDay.clicked()
        applyButton.clicked()

        compare(proxy.dateFilter, ActivityFilterProxyModel.CustomRange)
        // Assert via the same formatting the UI uses, so the round trip is
        // checked independently of the local time zone.
        compare(Qt.formatDate(proxy.rangeStart, "yyyy-MM-dd"), fromIso)
        compare(Qt.formatDate(proxy.rangeEnd, "yyyy-MM-dd"), toIso)
    }

    function test_calendar_click_after_complete_range_starts_fresh_range() {
        const calendar = createTemporaryObject(activityCalendarComponent, this)
        verify(calendar !== null)

        // A reopened calendar is seeded with the applied range; a further day
        // click must start a fresh range rather than being swallowed, so the
        // user can re-pick without hitting Reset first.
        calendar.seed(new Date(2001, 0, 10), new Date(2001, 0, 20))
        verify(calendar.valid)

        calendar.selectCalendarDate(new Date(2001, 0, 5))
        compare(calendar.startIso, "2001-01-05")
        compare(calendar.endIso, "")
        compare(calendar.pickingEnd, true)

        calendar.selectCalendarDate(new Date(2001, 0, 15))
        compare(calendar.startIso, "2001-01-05")
        compare(calendar.endIso, "2001-01-15")
        verify(calendar.valid)
    }

    function test_calendar_keyboard_focus_helpers() {
        const calendar = createTemporaryObject(activityCalendarComponent, this)
        verify(calendar !== null)

        // Offscreen tests cannot drive real key events through the popup, so
        // exercise the functions the grid's Keys handler calls directly.
        // A month far in the past keeps "today" out of the seeding path.
        calendar.calMonth = 0
        calendar.calYear = 2001
        calendar.customFrom = null
        calendar.calendarFocusDate = null

        calendar.ensureCalendarFocusDate()
        compare(calendar.isoDate(calendar.calendarFocusDate), "2001-01-01")

        // Left from the first of the month crosses into the previous month
        // and carries the displayed month along.
        calendar.moveCalendarFocus(-1)
        compare(calendar.isoDate(calendar.calendarFocusDate), "2000-12-31")
        compare(calendar.calMonth, 11)
        compare(calendar.calYear, 2000)

        // Down (a week) moves back into January.
        calendar.moveCalendarFocus(7)
        compare(calendar.isoDate(calendar.calendarFocusDate), "2001-01-07")
        compare(calendar.calMonth, 0)

        // A month step clamps the day to the target month's length.
        calendar.calendarFocusDate = new Date(2001, 0, 31)
        calendar.stepCalendarFocusMonth(1)
        compare(calendar.isoDate(calendar.calendarFocusDate), "2001-02-28")
        compare(calendar.calMonth, 1)

        // A picked start inside the displayed month seeds the focus ring.
        calendar.calendarFocusDate = null
        calendar.customFrom = new Date(2001, 1, 10)
        calendar.ensureCalendarFocusDate()
        compare(calendar.isoDate(calendar.calendarFocusDate), "2001-02-10")
    }

    function test_custom_date_range_reopen_preserves_dates() {
        const page = createTemporaryObject(activityComponent, this)
        verify(page !== null)

        const proxy = findChild(page, "activityFilterProxyModel")
        const activityPage = findChild(page, "activityPage")
        const calendar = createTemporaryObject(activityCalendarComponent, this)
        verify(proxy !== null)
        verify(activityPage !== null)
        verify(calendar !== null)

        // Apply a fixed range through the timezone-safe ISO setter.
        proxy.setCustomRange("2025-06-10", "2025-06-20")

        // Reopening the date popup re-seeds the calendar draft from
        // proxy.rangeStart / rangeEnd, which reach QML as UTC-midnight JS Dates.
        // modelDateToLocal must keep the same calendar day; before it, the local
        // formatting read them a day early west of UTC and Apply then shifted the
        // applied range by a day on every reopen. Drive the whole reopen path
        // (convert, seed, read back the ISO the Apply button sends to the model).
        calendar.seed(activityPage.modelDateToLocal(proxy.rangeStart),
                      activityPage.modelDateToLocal(proxy.rangeEnd))
        compare(calendar.startIso, "2025-06-10")
        compare(calendar.endIso, "2025-06-20")
    }
}

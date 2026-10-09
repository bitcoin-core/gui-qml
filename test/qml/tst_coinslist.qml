// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.2
import "../../qml/components"

TestCase {
    id: testCase
    name: "CoinsList"
    when: windowShown
    width: 900
    height: 700

    Component {
        id: coinsComponent
        CoinsList {
            wallet: testWalletModel
            coins: testCoinsListModel
            selectionMode: false
            width: 900
            height: 700
        }
    }

    function init() {
        testCoinsListModel.reset()
    }

    function test_lock_actions_reject_attempts_while_pending_data() {
        return [{tag: "lock", locked: true}, {tag: "unlock", locked: false}]
    }

    function test_lock_actions_reject_attempts_while_pending(data) {
        const view = createTemporaryObject(coinsComponent, testCase.Window.window.contentItem)
        verify(view !== null)
        view.walletSelectionActive = true
        const lock = findChild(view, "coinsLockButton")
        const unlock = findChild(view, "coinsUnlockButton")
        const toggle = findChild(view, "walletCoinLockToggle")
        const pending = findChild(view, "coinsLockPendingText")
        view.markedIds = ["coin-1"]
        tryCompare(lock, "enabled", true)
        tryCompare(unlock, "enabled", true)
        tryCompare(lock, "visible", true)
        verify(waitForRendering(view))
        const first = data.locked ? lock : unlock
        mouseClick(first, first.width / 2, first.height / 2)
        tryCompare(testCoinsListModel, "lockRequests", 1)
        verify(testCoinsListModel.lockOperationPending)
        verify(pending.visible)
        view.markedIds = ["coin-2"]
        verify(!lock.enabled)
        verify(!unlock.enabled)
        verify(!toggle.enabled)
        // The QML handlers also reject programmatic attempts while pending.
        view.lockMarked(!data.locked)
        view.contextCoinId = "coin-2"
        toggle.triggered()
        compare(testCoinsListModel.lockRequests, 1)
        compare(view.markedIds.length, 1)

        testCoinsListModel.finishLockOperation("")
        tryCompare(lock, "enabled", true)
        verify(unlock.enabled)
        verify(toggle.enabled)
        verify(!pending.visible)
        compare(testCoinsListModel.lockRequests, 1)
        verify(waitForRendering(view))
        const second = data.locked ? unlock : lock
        mouseClick(second, second.width / 2, second.height / 2)
        tryCompare(testCoinsListModel, "lockRequests", 2)
    }

    function test_context_action_shows_async_error_and_allows_retry() {
        const view = createTemporaryObject(coinsComponent, testCase.Window.window.contentItem)
        verify(view !== null)
        const toggle = findChild(view, "walletCoinLockToggle")
        const error = findChild(view, "coinsErrorText")
        view.contextCoinId = "coin-1"
        view.contextCoinLocked = false
        toggle.triggered()
        compare(testCoinsListModel.lockRequests, 1)
        verify(!error.visible)
        testCoinsListModel.finishLockOperation("Writing coin locks failed.")
        tryCompare(error, "visible", true)
        compare(error.text, "Writing coin locks failed.")
        verify(toggle.enabled)
        toggle.triggered()
        compare(testCoinsListModel.lockRequests, 2)
        tryCompare(error, "visible", false)
    }
}

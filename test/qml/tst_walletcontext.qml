// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
import QtQuick 2.15
import QtTest 1.2
import "../../qml/controls"

TestCase {
    name: "WalletContext"
    QtObject { id: otherWallet; property bool available: true }
    QtObject { id: host; property bool visible: true }
    WalletContext { id: context; wallet: walletController.selectedWallet; owner: host }
    function init() {
        walletController.setSelectedWalletObject(testWalletModel)
        host.visible = true
    }
    function cleanup() {
        context.invalidate()
        walletController.setSelectedWalletObject(testWalletModel)
    }
    function test_switch_back_does_not_revive_old_opening() {
        const old = context.begin()
        verify(context.isValid(old))
        walletController.setSelectedWalletObject(otherWallet)
        walletController.setSelectedWalletObject(testWalletModel)
        verify(!context.isValid(old))
        const fresh = context.begin()
        verify(!context.isValid(old))
        verify(context.isValid(fresh))
        compare(context.take(), testWalletModel)
        compare(context.take(), null)
    }
    function test_hiding_owner_rejects_delayed_action() {
        const old = context.begin()
        host.visible = false
        verify(!context.isValid(old))
        compare(context.take(), null)
    }
    function test_reselecting_current_wallet_preserves_opening() {
        const opening = context.begin()
        walletController.setSelectedWalletObject(testWalletModel)
        verify(context.isValid(opening))
    }
}

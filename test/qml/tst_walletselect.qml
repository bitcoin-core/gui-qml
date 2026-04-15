// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtTest 1.2
import "../../qml/pages/wallet"

TestCase {
    name: "WalletSelect"
    when: windowShown
    width: 360
    height: 240

    Component {
        id: walletSelectComponent

        WalletSelect {}
    }

    function init() {
        walletController.reset()
        walletListModel.reset()
    }

    function openWalletSelect() {
        const popup = createTemporaryObject(walletSelectComponent, this)
        verify(popup !== null)
        popup.open()
        tryCompare(popup, "opened", true)
        const list = findChild(popup, "walletSelectList")
        verify(list !== null)
        tryVerify(function() {
            return list.itemAtIndex(0) !== null && list.itemAtIndex(1) !== null
        })
        return popup
    }

    function test_close_button_tracks_load_state() {
        const popup = openWalletSelect()
        const list = findChild(popup, "walletSelectList")
        verify(list !== null)

        const openWalletClose = findChild(list.itemAtIndex(0), "walletSelectClose_testwallet")
        const closedWalletClose = findChild(list.itemAtIndex(1), "walletSelectClose_secondarywallet")
        verify(openWalletClose !== null)
        verify(closedWalletClose !== null)

        compare(openWalletClose.visible, true)
        compare(openWalletClose.enabled, true)
        compare(closedWalletClose.visible, false)
        compare(closedWalletClose.enabled, false)

        walletListModel.setWalletLoadState("testwallet", 0)
        tryCompare(openWalletClose, "visible", false)
        compare(openWalletClose.enabled, false)

        walletListModel.setWalletLoadState("secondarywallet", 1)
        tryCompare(closedWalletClose, "visible", true)
        compare(closedWalletClose.enabled, true)
    }

    function test_close_button_invokes_wallet_controller() {
        const popup = openWalletSelect()
        const list = findChild(popup, "walletSelectList")
        verify(list !== null)
        const openWalletClose = findChild(list.itemAtIndex(0), "walletSelectClose_testwallet")
        verify(openWalletClose !== null)
        verify(openWalletClose.visible)

        openWalletClose.clicked()

        compare(walletController.closeWalletCalls, 1)
        compare(walletController.lastClosedWalletName, "testwallet")
    }
}

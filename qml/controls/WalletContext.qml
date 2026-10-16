// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQml 2.15

InteractionContext {
    id: root
    property var wallet: null

    function capture() {
        return { wallet: wallet, selection: walletController.selectedWallet }
    }
    function accepts(context) {
        return context.wallet === wallet
            && context.selection === walletController.selectedWallet
            && (!wallet || wallet.available !== false)
    }
    function take() {
        const context = consume()
        return context ? context.wallet : null
    }
    onWalletChanged: invalidate()

    property Connections selectionChanges: Connections {
        target: walletController
        function onSelectedWalletChanged() { root.invalidate() }
    }
    property Connections walletChanges: Connections {
        target: root.wallet
        ignoreUnknownSignals: true
        function onAvailableChanged() { if (!root.wallet || root.wallet.available === false) root.invalidate() }
        function onDestroyed() { root.invalidate() }
    }
}

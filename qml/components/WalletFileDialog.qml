// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
import QtQml 2.15
import org.bitcoincore.qt 1.0
import "../controls"

AppFileDialog {
    id: root
    property var wallet: null
    property var owner: null
    property var operationTarget: null
    signal acceptedForWallet(var wallet, url file)
    property WalletContext context: WalletContext {
        wallet: root.wallet
        owner: root.owner
        target: root.operationTarget
        onInvalidated: root.close()
    }
    function openForWallet() {
        close()
        if (wallet && context.begin()) open()
    }
    onRejected: context.invalidate()
    onAccepted: {
        const file = selectedFile
        const originalWallet = context.take()
        if (originalWallet) acceptedForWallet(originalWallet, file)
    }
}

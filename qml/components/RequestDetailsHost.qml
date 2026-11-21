// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
import QtQuick 2.15
import org.bitcoincore.qt 1.0

Item {
    id: root
    property WalletQmlModel wallet: null
    property bool contextActive: true
    readonly property ReceiveRequestModal owned: loader.item as ReceiveRequestModal
    readonly property PaymentRequestModal compatibility: loader.item as PaymentRequestModal
    readonly property bool opened: owned ? owned.opened : compatibility ? compatibility.opened : false
    visible: owned ? owned.visible : compatibility ? compatibility.visible : false
    signal closed()
    function openRequest(id, origin) {
        if (owned) return owned.openRequest(id, origin)
        if (compatibility) return compatibility.openRequest(id, origin)
        return false
    }
    function close() {
        if (owned) { owned.cancelPending(); owned.close() }
        if (compatibility) compatibility.close()
    }
    Loader {
        id: loader
        sourceComponent: root.wallet && root.wallet.receive ? ownedDetails : compatibilityDetails
    }
    Component { id: ownedDetails; ReceiveRequestModal { wallet: root.wallet; contextActive: root.contextActive; onClosed: root.closed() } }
    Component { id: compatibilityDetails; PaymentRequestModal { wallet: root.wallet; onClosed: root.closed() } }
}

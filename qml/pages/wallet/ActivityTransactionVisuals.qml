// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import org.bitcoincore.qt 1.0

import "../../controls"

QtObject {
    id: root

    property int transactionType: 0
    property int transactionStatus: 0
    property bool isPendingRequest: false

    readonly property bool incoming: root.transactionType == Transaction.RecvWithAddress
        || root.transactionType == Transaction.RecvFromOther
        || root.transactionType == Transaction.Generated
    readonly property bool confirmedLike: root.transactionStatus == Transaction.Confirmed
        || root.transactionStatus == Transaction.Immature

    // Requests use the receive triangle like any incoming entry and stay
    // purple whether or not their address has been used: address use alone
    // does not prove the request was paid, so the green receive color is
    // reserved for real transaction rows.
    readonly property url iconSource: root.transactionType == Transaction.Generated
        ? "qrc:/icons/coinbase"
        : root.incoming ? "qrc:/icons/triangle-down" : "qrc:/icons/triangle-up"

    readonly property color iconColor: root.isPendingRequest
        ? Theme.color.purple
        : root.confirmedLike
            ? root.incoming ? Theme.color.green : Theme.color.orange
            : Theme.color.blue

    readonly property color amountColor: root.incoming ? Theme.color.green : Theme.color.neutral9
}

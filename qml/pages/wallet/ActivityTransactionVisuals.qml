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
    property bool countsForBalance: true

    readonly property bool incoming: root.transactionType == Transaction.RecvWithAddress
        || root.transactionType == Transaction.RecvFromOther
        || root.transactionType == Transaction.Generated
    readonly property bool confirmedLike: root.transactionStatus == Transaction.Confirmed
        || root.transactionStatus == Transaction.Immature
    readonly property bool zeroConf: !root.isPendingRequest
        && root.transactionStatus == Transaction.Unconfirmed

    readonly property url iconSource: root.zeroConf
        ? "qrc:/icons/pending"
        : root.transactionType == Transaction.Generated
            ? "qrc:/icons/coinbase"
            : root.incoming ? "qrc:/icons/triangle-down" : "qrc:/icons/triangle-up"

    readonly property color iconColor: root.isPendingRequest
        ? Theme.color.purple
        : root.confirmedLike
            ? root.incoming ? Theme.color.green : Theme.color.orange
            : Theme.color.blue

    // An amount that does not yet count for the balance must not carry the
    // settled-money color; the Widgets model greys such rows the same way
    // (immature coinbase keeps its color there too).
    readonly property color amountColor: !root.isPendingRequest && !root.countsForBalance
            && root.transactionStatus != Transaction.Immature
        ? Theme.color.neutral7
        : root.incoming ? Theme.color.green : Theme.color.neutral9
}

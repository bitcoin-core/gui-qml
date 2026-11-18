// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_REVIEWOWNER_H
#define BITCOIN_QML_REVIEWOWNER_H

#include <QObject>

class WalletQmlModelTransaction;

// GUI lifetime registration only. A review does not depend on its owner's
// wallet facade or gain access to wallet/domain operations through this port.
class ReviewOwner
{
public:
    virtual ~ReviewOwner() = default;
    virtual void registerReview(WalletQmlModelTransaction* review) = 0;
};

Q_DECLARE_INTERFACE(ReviewOwner, "org.bitcoincore.qt.reviewowner/1.0")

#endif // BITCOIN_QML_REVIEWOWNER_H

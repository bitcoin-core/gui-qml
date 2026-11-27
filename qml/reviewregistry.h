// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_REVIEWREGISTRY_H
#define BITCOIN_QML_REVIEWREGISTRY_H

#include <QObject>

class TransactionReviewModel;

// GUI lifetime registration only. A review does not depend on its owner's
// wallet facade or gain access to wallet/domain operations through this port.
class ReviewRegistry
{
public:
    virtual ~ReviewRegistry() = default;
    virtual void registerReview(TransactionReviewModel* review) = 0;
};

Q_DECLARE_INTERFACE(ReviewRegistry, "org.bitcoincore.qt.reviewregistry/1.0")

#endif // BITCOIN_QML_REVIEWREGISTRY_H

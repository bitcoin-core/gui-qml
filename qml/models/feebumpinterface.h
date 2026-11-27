// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_FEEBUMPINTERFACE_H
#define BITCOIN_QML_MODELS_FEEBUMPINTERFACE_H

#include <qml/models/feebumpoperation.h>
#include <qml/models/walletavailabilityinterface.h>

#include <QString>

/** What a FeeBumpController needs from a wallet. The wallet keeps every
 * attempt past its commit point until a page has shown its result, so an
 * unresolved replacement survives the page that started it and a second
 * commit for the same transaction is never offered meanwhile. */
class FeeBumpInterface : public WalletAvailabilityInterface
{
    Q_OBJECT
public:
    //! For `txid`, the kept attempt past its commit point if there is one
    //! (the caller shows or rechecks it), else a new review. Null when the
    //! wallet cannot start one; the wallet owns the attempt.
    virtual FeeBumpOperation* openFeeBump(const QString& txid, unsigned target_blocks) = 0;
    //! The caller showed this attempt's final result; the wallet forgets it.
    virtual void presented(FeeBumpOperation* attempt) = 0;
    //! The caller lets go: an attempt before its commit point is discarded,
    //! one past it is kept for the next open of its transaction.
    virtual void release(FeeBumpOperation* attempt) = 0;
protected:
    using WalletAvailabilityInterface::WalletAvailabilityInterface;
};

#endif // BITCOIN_QML_MODELS_FEEBUMPINTERFACE_H

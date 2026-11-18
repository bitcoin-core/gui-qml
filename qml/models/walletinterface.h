// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_WALLETINTERFACE_H
#define BITCOIN_QML_MODELS_WALLETINTERFACE_H

#include <QObject>

/** Base of every interface a wallet page's controller uses. A controller is
 * bound to one wallet's interface for its whole life (WalletPageController);
 * `available` tells it when that wallet can no longer be used. */
class WalletInterface : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
public:
    //! The wallet is loaded, not closing and not unloading.
    virtual bool available() const = 0;
Q_SIGNALS:
    void availableChanged();
protected:
    using QObject::QObject;
};

#endif // BITCOIN_QML_MODELS_WALLETINTERFACE_H

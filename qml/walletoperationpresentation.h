// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_WALLETOPERATIONPRESENTATION_H
#define BITCOIN_QML_WALLETOPERATIONPRESENTATION_H
#include <qml/walletcreation.h>
#include <QtPlugin>

// A typed GUI-only capability shared with component fixtures. This queue never
// discovers model properties dynamically or owns the underlying wallet work.
class WalletOperationPresentation {
public:
    virtual ~WalletOperationPresentation() = default;
    virtual QString presentationId() const = 0;
    virtual bool presentationBusy() const = 0;
    virtual bool presentationTerminal() const = 0;
    virtual bool presentationHasPrompt() const = 0;
    //! The prompt the operation waits on now, or null.
    virtual const QObject* presentationPrompt() const = 0;
    virtual WalletCreationReceipt dismissPresentation() = 0;
    virtual QMetaObject::Connection observePresentation(QObject* context, std::function<void()> changed) = 0;
};
Q_DECLARE_INTERFACE(WalletOperationPresentation, "org.bitcoincore.WalletOperationPresentation/1.0")
#endif // BITCOIN_QML_WALLETOPERATIONPRESENTATION_H

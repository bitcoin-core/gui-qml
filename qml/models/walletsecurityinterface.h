// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_WALLETSECURITYINTERFACE_H
#define BITCOIN_QML_MODELS_WALLETSECURITYINTERFACE_H

#include <qml/models/securityoperation.h>
#include <qml/models/walletavailabilityinterface.h>

#include <QObject>

/** What a WalletPasswordController needs from a wallet: whether it can set or
 * change the password, and the operations that do. WalletSecurityModel
 * implements it; QML tests use a fake. */
class WalletSecurityInterface : public WalletAvailabilityInterface
{
    Q_OBJECT
    Q_PROPERTY(bool canEncrypt READ canEncrypt NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool canChangePassphrase READ canChangePassphrase NOTIFY capabilitiesChanged)
    //! A password operation runs on this wallet.
    Q_PROPERTY(bool busy READ busy NOTIFY capabilitiesChanged)
public:
    using WalletAvailabilityInterface::WalletAvailabilityInterface;

    virtual bool canEncrypt() const = 0;
    virtual bool canChangePassphrase() const = 0;
    virtual bool busy() const = 0;
    //! Starts setting the first password. Returns null while another
    //! operation runs; the wallet owns the operation.
    virtual SecurityOperation* beginEncryption() = 0;
    //! Starts changing the password. Returns null while another runs.
    virtual SecurityOperation* beginPassphraseChange() = 0;

Q_SIGNALS:
    void capabilitiesChanged();
};

#endif // BITCOIN_QML_MODELS_WALLETSECURITYINTERFACE_H

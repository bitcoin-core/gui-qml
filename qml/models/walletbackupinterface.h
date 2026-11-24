// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_WALLETBACKUPINTERFACE_H
#define BITCOIN_QML_MODELS_WALLETBACKUPINTERFACE_H

#include <qml/models/filesaveoperation.h>
#include <qml/models/walletavailabilityinterface.h>

#include <QString>

/** What a WalletBackupController needs from a wallet. The wallet keeps one
 * backup at a time: `busy` lasts until the backend finishes the write or
 * retires it before writing, even when the page that started it is gone. */
class WalletBackupInterface : public WalletAvailabilityInterface
{
    Q_OBJECT
    //! A backup of this wallet is waiting for its destination or writing.
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
public:
    virtual bool busy() const = 0;
    //! Starts a backup to `path`. Returns null with `why` set when the wallet
    //! cannot start one now; the caller owns the operation.
    virtual FileSaveOperation* startBackup(const QString& path, QString& why) = 0;
Q_SIGNALS:
    void busyChanged();
protected:
    using WalletAvailabilityInterface::WalletAvailabilityInterface;
};

#endif // BITCOIN_QML_MODELS_WALLETBACKUPINTERFACE_H

// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_TRANSACTION_H
#define BITCOIN_QML_MODELS_TRANSACTION_H

#include "uint256.h"
#include <interfaces/wallet.h>

#include <QObject>
#include <QString>
#include <QSharedPointer>
#include <qglobal.h>
#include <qobject.h>

class Transaction : public QObject
{
    Q_OBJECT

public:
    enum Type {
        Other,
        Generated,
        SendToAddress,
        SendToOther,
        RecvWithAddress,
        RecvFromOther,
        SendToSelf
    };
    Q_ENUM(Type)

    enum Status {
        Confirmed,
        Unconfirmed,
        Confirming,
        Conflicted,
        Abandoned,
        Immature,
        NotAccepted
    };
    Q_ENUM(Status)

    Transaction(uint256 hash,
                qint64 time,
                Type type,
                const QString& address,
                CAmount debit,
                CAmount credit);

    Transaction(uint256 hash, qint64 time);

    QString prettyAmount() const;
    QString dateTimeString() const;
    void updateStatus(const interfaces::WalletTxStatus& wtx, int num_blocks, int64_t block_time);

    QString address{""};
    QString amount{""};
    CAmount credit{0};
    int depth{0};
    CAmount debit{0};
    uint256 hash{0};
    int idx{0};
    QString label{""};
    Status status;
    qint64 time;
    QString timestamp;
    Type type;
    QString txid;
    bool countsForBalance;

    static QList<QSharedPointer<Transaction>> fromWalletTx(const interfaces::WalletTx& tx);
};

#endif // BITCOIN_QML_MODELS_TRANSACTION_H

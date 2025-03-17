// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/transaction.h>

#include <interfaces/wallet.h>
#include <key_io.h>
#include <wallet/types.h>

#include <QDateTime>

using wallet::ISMINE_SPENDABLE;
using wallet::ISMINE_NO;
using wallet::ISMINE_WATCH_ONLY;
using wallet::isminetype;

namespace {
    const int RecommendedNumConfirmations = 6;
}

Transaction::Transaction(
    uint256 hash,
    qint64 time,
    Type type,
    const QString& address,
    CAmount debit,
    CAmount credit)
    : address(address)
    , credit(credit)
    , debit(debit)
    , hash(hash)
    , status(Unconfirmed)
    , time(time)
    , type(type)
    , involvesWatchAddress(false)
{
}

Transaction::Transaction(uint256 hash, qint64 time)
    : address("")
    , hash(hash)
    , time(time)
    , type(Type::Other)
    , involvesWatchAddress(false)
{
}

QString Transaction::prettyAmount() const
{
    CAmount net = credit - debit;
    QString sign = (net > 0) ? "+" : (net < 0) ? "-" : "";
    net = std::abs(net);

    qint64 bitcoins = net / 100000000;
    qint64 remainder = net % 100000000;

    QString result = QString("₿ %1%2.%3")
        .arg(sign)
        .arg(bitcoins)
        .arg(remainder, 8, 10, QChar('0'));

    return result;
}

QString Transaction::dateTimeString() const
{
    QDateTime dateTime = QDateTime::fromSecsSinceEpoch(time);
    QDateTime now = QDateTime::currentDateTimeUtc();

    qint64 elapsedSeconds = dateTime.secsTo(now);
    const qint64 minutes = elapsedSeconds / 60;
    if (minutes < 60) {
        return QString("%1 minute%2 ago")
            .arg(minutes)
            .arg(minutes == 1 ? "" : "s");
    }

    const qint64 hours = minutes / 60;
    if (hours < 24) {
        return QString("%1 hour%2 ago")
            .arg(hours)
            .arg(hours == 1 ? "" : "s");
    }

    int currentYear = QDate::currentDate().year();
    if (dateTime.date().year() == currentYear) {
        return dateTime.toString("MMMM d");
    } else {
        return dateTime.toString("MMMM d, yyyy");
    }
}

void Transaction::updateStatus(const interfaces::WalletTxStatus& wtx, int num_blocks, int64_t block_time)
{
    depth = wtx.depth_in_main_chain;
    if (type == Generated) {
        if (wtx.blocks_to_maturity > 0)
        {
            status = Immature;

            if (!wtx.is_in_main_chain)
            {
                status = NotAccepted;
            }
        }
        else
        {
            status = Confirmed;
        }
    }
    else
    {
        if (depth < 0)
        {
            status = Conflicted;
        }
        else if (depth == 0)
        {
            status = Unconfirmed;
            if (wtx.is_abandoned)
                status = Abandoned;
        }
        else if (depth < RecommendedNumConfirmations)
        {
            status = Confirming;
        }
        else
        {
            status = Confirmed;
        }
    }
}

QList<QSharedPointer<Transaction>> Transaction::fromWalletTx(const interfaces::WalletTx& wtx)
{
    QList<QSharedPointer<Transaction>> parts;
    int64_t nTime = wtx.time;
    CAmount nCredit = wtx.credit;
    CAmount nDebit = wtx.debit;
    CAmount nNet = nCredit - nDebit;
    uint256 hash = wtx.tx->GetHash();
    std::map<std::string, std::string> mapValue = wtx.value_map;

    bool involvesWatchAddress = false;
    isminetype fAllFromMe = ISMINE_SPENDABLE;
    bool any_from_me = false;
    if (wtx.is_coinbase) {
        fAllFromMe = ISMINE_NO;
    } else {
        for (const isminetype mine : wtx.txin_is_mine)
        {
            if(mine & ISMINE_WATCH_ONLY) involvesWatchAddress = true;
            if(fAllFromMe > mine) fAllFromMe = mine;
            if (mine) any_from_me = true;
        }
    }

    if (fAllFromMe || !any_from_me) {
        for (const isminetype mine : wtx.txout_is_mine)
        {
            if(mine & ISMINE_WATCH_ONLY) involvesWatchAddress = true;
        }

        CAmount nTxFee = nDebit - wtx.tx->GetValueOut();


        for(unsigned int i = 0; i < wtx.tx->vout.size(); i++)
        {
            const CTxOut& txout = wtx.tx->vout[i];

            if (fAllFromMe) {
                // Change is only really possible if we're the sender
                // Otherwise, someone just sent bitcoins to a change address, which should be shown
                //if (wtx.txout_is_change[i]) {
                //   continue;
                //}

                //
                // Debit
                //

                QSharedPointer<Transaction> sub = QSharedPointer<Transaction>::create(hash, nTime);
                sub->idx = i;
                sub->involvesWatchAddress = involvesWatchAddress;

                if (!std::get_if<CNoDestination>(&wtx.txout_address[i]))
                {
                    // Sent to Bitcoin Address
                    sub->type = Transaction::SendToAddress;
                    sub->address = QString::fromStdString(EncodeDestination(wtx.txout_address[i]));
                }
                else
                {
                    // Sent to IP, or other non-address transaction like OP_EVAL
                    sub->type = Transaction::SendToOther;
                    sub->address = QString::fromStdString(mapValue["to"]);
                }

                CAmount nValue = txout.nValue;
                /* Add fee to first output */
                if (nTxFee > 0)
                {
                    nValue += nTxFee;
                    nTxFee = 0;
                }
                sub->debit = -nValue;

                parts.append(sub);
            }

            isminetype mine = wtx.txout_is_mine[i];
            if(mine)
            {
                //
                // Credit
                //

                QSharedPointer<Transaction> sub = QSharedPointer<Transaction>::create(hash, nTime);
                sub->idx = i; // vout index
                sub->credit = txout.nValue;
                sub->involvesWatchAddress = mine & ISMINE_WATCH_ONLY;
                if (wtx.txout_address_is_mine[i])
                {
                    // Received by Bitcoin Address
                    sub->type = Transaction::RecvWithAddress;
                    sub->address = QString::fromStdString(EncodeDestination(wtx.txout_address[i]));
                }
                else
                {
                    // Received by IP connection (deprecated features), or a multisignature or other non-simple transaction
                    sub->type = Transaction::RecvFromOther;
                    sub->address = QString::fromStdString(mapValue["from"]);
                }
                if (wtx.is_coinbase)
                {
                    // Generated
                    sub->type = Transaction::Generated;
                }

                parts.append(sub);
            }
        }
    } else {
        //
        // Mixed debit transaction, can't break down payees
        //
        parts.append(QSharedPointer<Transaction>::create(hash, nTime, Transaction::Other, "", nNet, 0));
        parts.last()->involvesWatchAddress = involvesWatchAddress;
    }

    return parts;
}

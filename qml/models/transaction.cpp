// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/transaction.h>

#include <qml/bitcoinunits.h>
#include <interfaces/wallet.h>
#include <key_io.h>
#include <wallet/types.h>

#include <QDateTime>

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
    , txid(QString::fromStdString(hash.GetHex()))
    , involvesWatchAddress(false)
{
}

Transaction::Transaction(uint256 hash, qint64 time)
    : address("")
    , hash(hash)
    , time(time)
    , type(Type::Other)
    , txid(QString::fromStdString(hash.GetHex()))
    , involvesWatchAddress(false)
{
}

CAmount Transaction::netAmount() const
{
    return credit + debit;
}

QString Transaction::prettyAmount(int display_unit) const
{
    const CAmount net = netAmount();
    const bool plus_sign = (net > 0);
    QmlBitcoinUnits::Unit unit = (display_unit == 1)
        ? QmlBitcoinUnits::Unit::SAT
        : QmlBitcoinUnits::Unit::BTC;
    return QmlBitcoinUnits::format(unit, net, plus_sign);
}

QString Transaction::dateTimeString() const
{
    if (isPendingRequest) return tr("Pending receive");

    QDateTime dateTime = QDateTime::fromSecsSinceEpoch(time);
    QDateTime now = QDateTime::currentDateTimeUtc();

    qint64 elapsedSeconds = dateTime.secsTo(now);
    const qint64 minutes = elapsedSeconds / 60;
    if (minutes < 60) {
        return minutes == 1 ? tr("1 minute ago") : tr("%1 minutes ago").arg(minutes);
    }

    const qint64 hours = minutes / 60;
    if (hours < 24) {
        return hours == 1 ? tr("1 hour ago") : tr("%1 hours ago").arg(hours);
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
    uint256 hash = wtx.tx->GetHash().ToUint256();
    QString txidStr = QString::fromStdString(hash.GetHex());
    std::map<std::string, std::string> mapValue = wtx.value_map;

    QString replacesTxid;
    QString replacedByTxid;
    if (mapValue.count("replaces_txid")) {
        replacesTxid = QString::fromStdString(mapValue["replaces_txid"]);
    }
    if (mapValue.count("replaced_by_txid")) {
        replacedByTxid = QString::fromStdString(mapValue["replaced_by_txid"]);
    }

    bool involvesWatchAddress = false;
    bool fAllFromMe = true;
    bool any_from_me = false;
    if (wtx.is_coinbase) {
        fAllFromMe = false;
    } else {
        for (const bool mine : wtx.txin_is_mine)
        {
            if (!mine) fAllFromMe = false;
            if (mine) any_from_me = true;
        }
    }

    if (fAllFromMe || !any_from_me) {
        CAmount nTxFee = nDebit - wtx.tx->GetValueOut();


        for(unsigned int i = 0; i < wtx.tx->vout.size(); i++)
        {
            const CTxOut& txout = wtx.tx->vout[i];

            if (fAllFromMe) {
                // Only hide change when this wallet is the sender. If someone sends to
                // one of our change addresses, it is still an incoming payment.
                if (wtx.txout_is_change[i]) {
                    continue;
                }

                //
                // Debit
                //

                QSharedPointer<Transaction> sub = QSharedPointer<Transaction>::create(hash, nTime);
                sub->idx = i;
                sub->txid = txidStr;
                sub->replacesTxid = replacesTxid;
                sub->replacedByTxid = replacedByTxid;
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
                    sub->fee = nTxFee;
                    nValue += nTxFee;
                    nTxFee = 0;
                }
                sub->debit = -nValue;

                parts.append(sub);
            }

            bool mine = wtx.txout_is_mine[i];
            if(mine)
            {
                //
                // Credit
                //

                QSharedPointer<Transaction> sub = QSharedPointer<Transaction>::create(hash, nTime);
                sub->idx = i; // vout index
                sub->txid = txidStr;
                sub->replacesTxid = replacesTxid;
                sub->replacedByTxid = replacedByTxid;
                sub->credit = txout.nValue;
                sub->involvesWatchAddress = false;
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
        parts.last()->txid = txidStr;
        parts.last()->replacesTxid = replacesTxid;
        parts.last()->replacedByTxid = replacedByTxid;
        parts.last()->involvesWatchAddress = involvesWatchAddress;
    }

    return parts;
}

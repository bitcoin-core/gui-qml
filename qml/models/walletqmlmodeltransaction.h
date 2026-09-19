// Copyright (c) 2011-2025 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_WALLETQMLMODELTRANSACTION_H
#define BITCOIN_QML_MODELS_WALLETQMLMODELTRANSACTION_H

#include <qml/bitcoinamount.h>
#include <qml/models/sendrecipientslistmodel.h>

#include <consensus/amount.h>
#include <primitives/transaction.h>

#include <QMap>

class WalletQmlModelTransaction : public QObject
{
    Q_OBJECT
    Q_PROPERTY(BitcoinAmount* amountAmount READ amountAmount CONSTANT)
    Q_PROPERTY(BitcoinAmount* feeAmount READ feeAmount CONSTANT)
    Q_PROPERTY(BitcoinAmount* totalAmount READ totalAmount CONSTANT)
    Q_PROPERTY(QString amount READ amount NOTIFY amountChanged)
    Q_PROPERTY(QString fee READ fee NOTIFY feeChanged)
    Q_PROPERTY(QString total READ total NOTIFY totalChanged)
    Q_PROPERTY(QString label READ label CONSTANT)
    Q_PROPERTY(QString txid READ txid NOTIFY txidChanged)
public:
    explicit WalletQmlModelTransaction(const SendRecipientsListModel* recipient, QObject* parent = nullptr);

    BitcoinAmount* amountAmount() const;
    BitcoinAmount* feeAmount() const;
    BitcoinAmount* totalAmount() const;
    QString amount() const;
    QString fee() const;
    QString total() const;
    QString label() const;
    QString txid() const;
    const QMap<QString, QString>& recipientLabels() const { return m_recipient_labels; }

    CTransactionRef& getWtx();
    void setWtx(const CTransactionRef&);

    void setTransactionFee(const CAmount& newFee);
    CAmount getTransactionFee() const;

    CAmount getTotalTransactionAmount() const;

    void setDisplayUnit(int unit);

    void reassignAmounts(int nChangePosRet); // needed for the subtract-fee-from-amount feature

Q_SIGNALS:
    void amountChanged();
    void feeChanged();
    void totalChanged();
    void txidChanged();

private:
    static QString formatWithUnit(CAmount value, int display_unit);

    QString m_address;
    QString m_label;
    // Keep the reviewed notes even if the send form is edited or reset later.
    QMap<QString, QString> m_recipient_labels;
    CAmount m_amount;
    CAmount m_fee;
    BitcoinAmount* m_amount_amount;
    BitcoinAmount* m_fee_amount;
    BitcoinAmount* m_total_amount;
    CTransactionRef m_wtx;
    int m_display_unit{0};
};

#endif // BITCOIN_QML_MODELS_WALLETQMLMODELTRANSACTION_H

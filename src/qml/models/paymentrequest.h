// Copyright (c) 2024-2025 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_PAYMENTREQUEST_H
#define BITCOIN_QML_MODELS_PAYMENTREQUEST_H

#include <qml/bitcoinamount.h>

#include <addresstype.h>

#include <QObject>

class PaymentRequest : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString address READ address NOTIFY addressChanged)
    Q_PROPERTY(QString label READ label WRITE setLabel NOTIFY labelChanged)
    Q_PROPERTY(QString message READ message WRITE setMessage NOTIFY messageChanged)
    Q_PROPERTY(BitcoinAmount* amount READ amount CONSTANT)
    Q_PROPERTY(QString amountError READ amountError NOTIFY amountErrorChanged)
    Q_PROPERTY(QString id READ id NOTIFY idChanged)

public:
    explicit PaymentRequest(QObject* parent = nullptr);

    QString address() const;

    QString label() const;
    void setLabel(const QString& label);

    QString message() const;
    void setMessage(const QString& message);

    BitcoinAmount* amount() const;
    QString amountError() const;
    void setAmountError(const QString& error);

    QString id() const;
    void setId(unsigned int id);

    void setDestination(const CTxDestination& destination);
    CTxDestination destination() const;

    Q_INVOKABLE void clear();

Q_SIGNALS:
    void addressChanged();
    void labelChanged();
    void messageChanged();
    void amountErrorChanged();
    void idChanged();

private:
    CTxDestination m_destination;
    QString m_label;
    QString m_message;
    QString m_amountError;
    BitcoinAmount* m_amount;
    QString m_id;
};

#endif // BITCOIN_QML_MODELS_PAYMENTREQUEST_H

// Copyright (c) 2025 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_SENDRECIPIENT_H
#define BITCOIN_QML_MODELS_SENDRECIPIENT_H

#include <qml/bitcoinamount.h>

#include <QObject>
#include <QString>

class WalletQmlModel;

class SendRecipient : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString address READ address WRITE setAddress NOTIFY addressChanged)
    Q_PROPERTY(QString label READ label WRITE setLabel NOTIFY labelChanged)
    Q_PROPERTY(QString message READ message WRITE setMessage NOTIFY messageChanged)
    Q_PROPERTY(BitcoinAmount* amount READ amount CONSTANT)

    Q_PROPERTY(QString addressError READ addressError NOTIFY addressErrorChanged)
    Q_PROPERTY(QString amountError READ amountError NOTIFY amountErrorChanged)
    Q_PROPERTY(bool isValid READ isValid NOTIFY isValidChanged)

public:
    explicit SendRecipient(WalletQmlModel* wallet, QObject* parent = nullptr);

    QString address() const;
    void setAddress(const QString& address);
    QString addressError() const;
    void setAddressError(const QString& error);

    QString label() const;
    void setLabel(const QString& label);

    BitcoinAmount* amount() const;
    QString amountError() const;
    void setAmountError(const QString& error);

    QString message() const;
    void setMessage(const QString& message);

    CAmount cAmount() const;

    bool subtractFeeFromAmount() const;

    bool isValid() const;

    Q_INVOKABLE void clear();

Q_SIGNALS:
    void addressChanged();
    void addressErrorChanged();
    void amountErrorChanged();
    void labelChanged();
    void messageChanged();
    void isValidChanged();

private:
    void validateAddress();
    void validateAmount();

    WalletQmlModel* m_wallet;
    QString m_address{""};
    QString m_addressError{""};
    QString m_label{""};
    QString m_message{""};
    BitcoinAmount* m_amount;
    QString m_amountError{""};
    bool m_subtractFeeFromAmount{false};
};

#endif // BITCOIN_QML_MODELS_SENDRECIPIENT_H

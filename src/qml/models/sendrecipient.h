// Copyright (c) 2025 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_SENDRECIPIENT_H
#define BITCOIN_QML_MODELS_SENDRECIPIENT_H

#include <QObject>
#include <QString>
#include <qml/bitcoinamount.h>

class SendRecipient : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString address READ address WRITE setAddress NOTIFY addressChanged)
    Q_PROPERTY(QString label READ label WRITE setLabel NOTIFY labelChanged)
    Q_PROPERTY(QString amount READ amount WRITE setAmount NOTIFY amountChanged)
    Q_PROPERTY(QString message READ message WRITE setMessage NOTIFY messageChanged)

public:
    explicit SendRecipient(QObject* parent = nullptr);

    QString address() const;
    void setAddress(const QString& address);

    QString label() const;
    void setLabel(const QString& label);

    QString amount() const;
    void setAmount(const QString& amount);

    QString message() const;
    void setMessage(const QString& message);

    CAmount cAmount() const;

    bool subtractFeeFromAmount() const;

    Q_INVOKABLE void clear();

Q_SIGNALS:
    void addressChanged();
    void labelChanged();
    void amountChanged();
    void messageChanged();

private:
    QString m_address;
    QString m_label;
    QString m_amount;
    QString m_message;
    bool m_subtractFeeFromAmount{false};
};

#endif // BITCOIN_QML_MODELS_SENDRECIPIENT_H

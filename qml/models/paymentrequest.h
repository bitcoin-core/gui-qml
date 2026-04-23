// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_PAYMENTREQUEST_H
#define BITCOIN_QML_MODELS_PAYMENTREQUEST_H

#include <qml/bitcoinamount.h>

#include <addresstype.h>

#include <QDateTime>
#include <QObject>
#include <QString>

class PaymentRequest : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString address READ address NOTIFY addressChanged)
    Q_PROPERTY(QString addressFormatted READ addressFormatted NOTIFY addressChanged)
    Q_PROPERTY(QString label READ label WRITE setLabel NOTIFY labelChanged)
    Q_PROPERTY(QString message READ message WRITE setMessage NOTIFY messageChanged)
    Q_PROPERTY(QString noteSelf READ noteSelf WRITE setNoteSelf NOTIFY noteSelfChanged)
    Q_PROPERTY(BitcoinAmount* amount READ amount CONSTANT)
    Q_PROPERTY(QString addressType READ addressType NOTIFY addressChanged)
    Q_PROPERTY(QString amountError READ amountError NOTIFY amountErrorChanged)
    Q_PROPERTY(QString id READ id NOTIFY idChanged)
    Q_PROPERTY(QString qrPayload READ qrPayload NOTIFY qrPayloadChanged)
    Q_PROPERTY(QString createdIso READ createdIso NOTIFY createdIsoChanged)
    Q_PROPERTY(bool hasPaymentInfo READ hasPaymentInfo NOTIFY qrPayloadChanged)

public:
    explicit PaymentRequest(QObject* parent = nullptr);

    QString address() const;
    QString addressFormatted() const;
    QString addressType() const;

    QString label() const;
    void setLabel(const QString& label);

    QString message() const;
    void setMessage(const QString& message);

    QString noteSelf() const;
    void setNoteSelf(const QString& note);

    BitcoinAmount* amount() const;
    QString amountError() const;
    void setAmountError(const QString& error);

    QString id() const;
    void setId(unsigned int id);

    void setDestination(const CTxDestination& destination);
    CTxDestination destination() const;

    QString qrPayload() const;

    QString createdIso() const;
    void setCreated(const QDateTime& dt);
    bool hasPaymentInfo() const;

    Q_INVOKABLE void clear();

Q_SIGNALS:
    void addressChanged();
    void labelChanged();
    void messageChanged();
    void noteSelfChanged();
    void amountErrorChanged();
    void idChanged();
    void qrPayloadChanged();
    void createdIsoChanged();

private:
    static QString FormatAddress(const QString& address);

    CTxDestination m_destination;
    QString m_label;
    QString m_message;
    QString m_noteSelf;
    QString m_amountError;
    BitcoinAmount* m_amount;
    QString m_id;
    QDateTime m_created;
};

#endif // BITCOIN_QML_MODELS_PAYMENTREQUEST_H

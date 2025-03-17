// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_BITCOINAMOUNT_H
#define BITCOIN_QML_BITCOINAMOUNT_H

#include <QObject>
#include <QString>
#include <qobjectdefs.h>

class BitcoinAmount : public QObject
{
    Q_OBJECT
    Q_PROPERTY(Unit unit READ unit WRITE setUnit NOTIFY unitChanged)
    Q_PROPERTY(QString unitLabel READ unitLabel NOTIFY unitChanged)
    Q_PROPERTY(QString amount READ amount WRITE setAmount NOTIFY amountChanged)

public:
    enum class Unit {
        BTC,
        SAT
    };
    Q_ENUM(Unit)

    explicit BitcoinAmount(QObject *parent = nullptr);

    Unit unit() const;
    void setUnit(Unit unit);
    QString unitLabel() const;
    QString amount() const;
    void setAmount(const QString& new_amount);

public Q_SLOTS:
    QString sanitize(const QString &text);
    QString convert(const QString &text, Unit unit);

Q_SIGNALS:
    void unitChanged();
    void unitLabelChanged();
    void amountChanged();

private:
    long long toSatoshis(QString &amount, const Unit unit);
    int decimals(Unit unit);

    Unit m_unit;
    QString m_unitLabel;
    QString m_amount;
};

#endif // BITCOIN_QML_BITCOINAMOUNT_H

// Copyright (c) 2024-2025 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_BITCOINAMOUNT_H
#define BITCOIN_QML_BITCOINAMOUNT_H

#include <consensus/amount.h>

#include <QObject>
#include <QString>

class BitcoinAmount : public QObject
{
    Q_OBJECT
    Q_PROPERTY(Unit unit READ unit WRITE setUnit NOTIFY unitChanged)
    Q_PROPERTY(QString unitLabel READ unitLabel NOTIFY unitChanged)
    Q_PROPERTY(QString display READ toDisplay WRITE fromDisplay NOTIFY amountChanged)
    Q_PROPERTY(qint64 satoshi READ satoshi WRITE setSatoshi NOTIFY amountChanged)

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

    QString toDisplay() const;
    void fromDisplay(const QString& new_amount);
    qint64 satoshi() const;
    void setSatoshi(qint64 new_amount);

    bool isSet() const { return m_isSet; }

    static QString satsToBtcString(qint64 sat);

public Q_SLOTS:
    void flipUnit();
    void clear();

Q_SIGNALS:
    void unitChanged();
    void amountChanged();

private:
    QString sanitize(const QString& text);
    static qint64 btcToSats(const QString& btc);

    qint64 m_satoshi{0};
    bool m_isSet{false};
    Unit m_unit{Unit::BTC};
};

#endif // BITCOIN_QML_BITCOINAMOUNT_H

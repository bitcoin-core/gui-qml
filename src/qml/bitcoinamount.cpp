// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/bitcoinamount.h>

#include <QRegExp>
#include <QStringList>


BitcoinAmount::BitcoinAmount(QObject *parent) : QObject(parent)
{
    m_unit = Unit::BTC;
}

int BitcoinAmount::decimals(Unit unit)
{
    switch (unit) {
    case Unit::BTC: return 8;
    case Unit::SAT: return 0;
    } // no default case, so the compiler can warn about missing cases
    assert(false);
}

QString BitcoinAmount::sanitize(const QString &text)
{
    QString result = text;

    // Remove any invalid characters
    result.remove(QRegExp("[^0-9.]"));

    // Ensure only one decimal point
    QStringList parts = result.split('.');
    if (parts.size() > 2) {
        result = parts[0] + "." + parts[1];
    }

    // Limit decimal places to 8
    if (parts.size() == 2 && parts[1].length() > 8) {
        result = parts[0] + "." + parts[1].left(8);
    }

    return result;
}

BitcoinAmount::Unit BitcoinAmount::unit() const
{
    return m_unit;
}

void BitcoinAmount::setUnit(const Unit unit)
{
    m_unit = unit;
    Q_EMIT unitChanged();
}

QString BitcoinAmount::unitLabel() const
{
    switch (m_unit) {
    case Unit::BTC: return "₿";
    case Unit::SAT: return "Sat";
    }
    assert(false);
}

QString BitcoinAmount::amount() const
{
    return m_amount;
}

void BitcoinAmount::setAmount(const QString& new_amount)
{
    m_amount = sanitize(new_amount);
    Q_EMIT amountChanged();
}

long long BitcoinAmount::toSatoshis(QString& amount, const Unit unit)
{

    int num_decimals = decimals(unit);

    QStringList parts = amount.remove(' ').split(".");

    QString whole = parts[0];
    QString decimals;

    if(parts.size() > 1)
    {
        decimals = parts[1];
    }
    QString str = whole + decimals.leftJustified(num_decimals, '0', true);

    return str.toLongLong();
}

QString BitcoinAmount::convert(const QString &amount, Unit unit)
{
    if (amount == "") {
        return amount;
    }

    QString result = amount;
    int decimalPosition  = result.indexOf(".");

    if (decimalPosition == -1) {
        decimalPosition = result.length();
        result.append(".");
    }

    if (unit == Unit::BTC) {
        int numDigitsAfterDecimal = result.length() - decimalPosition - 1;
        if (numDigitsAfterDecimal < 8) {
            result.append(QString(8 - numDigitsAfterDecimal, '0'));
        }
        result.remove(decimalPosition, 1);

        while (result.startsWith('0') && result.length() > 1) {
            result.remove(0, 1);
        }
    } else if (unit == Unit::SAT) {
        result.remove(decimalPosition, 1);
        int newDecimalPosition = decimalPosition - 8;
        if (newDecimalPosition < 1) {
            result = QString("0").repeated(-newDecimalPosition) + result;
            newDecimalPosition = 0;
        }
        result.insert(newDecimalPosition, ".");

        while (result.endsWith('0') && result.contains('.')) {
            result.chop(1);
        }
        if (result.endsWith('.')) {
            result.chop(1);
        }
        if (result.startsWith('.')) {
            result.insert(0, "0");
        }
    }

    return result;
}

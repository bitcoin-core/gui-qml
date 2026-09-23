// Copyright (c) 2024-2025 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/bitcoinamount.h>

#include <qml/bitcoinunits.h>

#include <cassert>
#include <limits>

#include <QRegularExpression>
#include <QStringList>

namespace {
qint64 UnitFactor(BitcoinAmount::Unit unit)
{
    switch (unit) {
    case BitcoinAmount::Unit::BTC: return COIN;
    case BitcoinAmount::Unit::mBTC: return 100'000;
    case BitcoinAmount::Unit::uBTC: return 100;
    case BitcoinAmount::Unit::SAT: return 1;
    }
    assert(false);
}

int UnitDecimals(BitcoinAmount::Unit unit)
{
    switch (unit) {
    case BitcoinAmount::Unit::BTC: return 8;
    case BitcoinAmount::Unit::mBTC: return 5;
    case BitcoinAmount::Unit::uBTC: return 2;
    case BitcoinAmount::Unit::SAT: return 0;
    }
    assert(false);
}

QmlBitcoinUnits::Unit ToQmlUnit(BitcoinAmount::Unit unit)
{
    switch (unit) {
    case BitcoinAmount::Unit::BTC: return QmlBitcoinUnits::Unit::BTC;
    case BitcoinAmount::Unit::mBTC: return QmlBitcoinUnits::Unit::mBTC;
    case BitcoinAmount::Unit::uBTC: return QmlBitcoinUnits::Unit::uBTC;
    case BitcoinAmount::Unit::SAT: return QmlBitcoinUnits::Unit::SAT;
    }
    assert(false);
}
} // namespace

BitcoinAmount::BitcoinAmount(QObject* parent)
    : QObject(parent)
{
}

QString BitcoinAmount::sanitize(const QString &text) const
{
    QString result = text;

    // Remove any invalid characters
    result.remove(QRegularExpression("[^0-9.]"));

    // Ensure only one decimal point
    QStringList parts = result.split('.');
    if (parts.size() > 2) {
        result = parts[0] + "." + parts[1];
    }

    // Limit decimal places to the selected display unit.
    const int decimals = UnitDecimals(m_unit);
    if (parts.size() == 2 && parts[1].length() > decimals) {
        result = parts[0] + "." + parts[1].left(decimals);
    }

    return result;
}

qint64 BitcoinAmount::satoshi() const
{
    return m_satoshi;
}

void BitcoinAmount::setSatoshi(qint64 new_amount)
{
    if (m_satoshi != new_amount || !m_isSet) {
        const bool label_changes = (m_unit == Unit::SAT) &&
                                   ((qAbs(m_satoshi) == 1) != (qAbs(new_amount) == 1));
        m_isSet = true;
        m_satoshi = new_amount;
        if (label_changes) Q_EMIT unitChanged();
        Q_EMIT amountChanged();
        Q_EMIT displayChanged();
        Q_EMIT displayWithUnitChanged();
    }
}

void BitcoinAmount::clear()
{
    if (!m_isSet && m_satoshi == 0) {
        return;
    }
    const bool label_changes = (m_unit == Unit::SAT) && (qAbs(m_satoshi) == 1);
    m_satoshi = 0;
    m_isSet = false;
    if (label_changes) Q_EMIT unitChanged();
    Q_EMIT amountChanged();
    Q_EMIT displayChanged();
    Q_EMIT displayWithUnitChanged();
}

BitcoinAmount::Unit BitcoinAmount::unit() const
{
    return m_unit;
}

void BitcoinAmount::setUnit(const Unit unit)
{
    if (m_unit == unit) return;
    m_unit = unit;
    Q_EMIT unitChanged();
    Q_EMIT displayChanged();
    Q_EMIT displayWithUnitChanged();
}

QString BitcoinAmount::unitLabel() const
{
    return QmlBitcoinUnits::displayLabel(ToQmlUnit(m_unit), m_satoshi);
}

void BitcoinAmount::flipUnit()
{
    m_unit = m_unit == Unit::SAT ? Unit::BTC : Unit::SAT;
    Q_EMIT unitChanged();
    Q_EMIT displayChanged();
    Q_EMIT displayWithUnitChanged();
}

QString BitcoinAmount::satsToBtcString(qint64 sat)
{
    const bool negative = sat < 0;
    qint64 absSat = negative ? -sat : sat;

    const qint64 wholePart = absSat / COIN;
    const qint64 fracInt = absSat % COIN;
    QString fracPart = QString("%1").arg(fracInt, 8, 10, QLatin1Char('0'));

    QString result = QString::number(wholePart) + '.' + fracPart;
    if (negative) {
        result.prepend('-');
    }
    return result;
}

QString BitcoinAmount::toDisplay() const
{
    if (!m_isSet) {
        return "";
    }
    return QmlBitcoinUnits::format(ToQmlUnit(m_unit), m_satoshi, false, QmlBitcoinUnits::SeparatorStyle::NEVER);
}

QString BitcoinAmount::displayWithUnit() const
{
    const QString display{toDisplay()};
    return display.isEmpty() ? QString{} : display + QStringLiteral(" ") + unitLabel();
}

QString BitcoinAmount::localizedDisplayWithUnit() const
{
    if (!m_isSet) return {};
    const auto unit = ToQmlUnit(m_unit);
    return QmlBitcoinUnits::formatForDisplay(unit, m_satoshi) + QLatin1Char(' ')
        + QmlBitcoinUnits::label(unit);
}

qint64 BitcoinAmount::displayToSats(const QString& sanitized) const
{
    if (sanitized.isEmpty() || sanitized == ".") return 0;

    QString cleaned = sanitized;
    if (cleaned.startsWith('.')) cleaned.prepend('0');

    QStringList parts = cleaned.split('.');
    const qint64 whole = parts[0].isEmpty() ? 0 : parts[0].toLongLong();
    qint64 frac = 0;
    const int decimals = UnitDecimals(m_unit);
    if (parts.size() == 2) {
        frac = parts[1].leftJustified(decimals, '0').toLongLong();
    }

    const qint64 factor = UnitFactor(m_unit);
    if (whole > std::numeric_limits<qint64>::max() / factor) {
        return std::numeric_limits<qint64>::max();
    }

    return whole * factor + frac;
}

void BitcoinAmount::fromDisplay(const QString& text)
{
    if (text.trimmed().isEmpty()) {
        clear();
        return;
    }

    qint64 newSat = 0;
    if (m_unit == Unit::SAT) {
        QString digitsOnly = text;
        digitsOnly.remove(QRegularExpression("[^0-9]"));
        newSat = digitsOnly.trimmed().isEmpty() ? 0 : digitsOnly.toLongLong();
    } else {
        QString sanitized = sanitize(text);
        newSat = displayToSats(sanitized);
    }
    setSatoshi(newSat);
}

void BitcoinAmount::format()
{
    Q_EMIT displayChanged();
    Q_EMIT displayWithUnitChanged();
}

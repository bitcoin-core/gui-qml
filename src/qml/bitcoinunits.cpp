// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/bitcoinunits.h>

#include <cassert>

#include <QtGlobal>

namespace {
constexpr int THIN_SP_CP = 0x2009;
}

qint64 QmlBitcoinUnits::factor(Unit unit)
{
    switch (unit) {
    case Unit::BTC: return 100'000'000;
    case Unit::mBTC: return 100'000;
    case Unit::uBTC: return 100;
    case Unit::SAT: return 1;
    }
    assert(false);
}

int QmlBitcoinUnits::decimals(Unit unit)
{
    switch (unit) {
    case Unit::BTC: return 8;
    case Unit::mBTC: return 5;
    case Unit::uBTC: return 2;
    case Unit::SAT: return 0;
    }
    assert(false);
}

QString QmlBitcoinUnits::format(Unit unit, CAmount amount, bool plussign, SeparatorStyle separators)
{
    const qint64 n = static_cast<qint64>(amount);
    const qint64 coin = factor(unit);
    const int num_decimals = decimals(unit);
    const qint64 n_abs = (n > 0 ? n : -n);
    const qint64 quotient = n_abs / coin;

    QString quotient_str = QString::number(quotient);
    QChar thin_sp(THIN_SP_CP);
    const int q_size = quotient_str.size();
    if (separators == SeparatorStyle::ALWAYS || (separators == SeparatorStyle::STANDARD && q_size > 4)) {
        for (int i = 3; i < q_size; i += 3) {
            quotient_str.insert(q_size - i, thin_sp);
        }
    }

    if (n < 0) {
        quotient_str.insert(0, '-');
    } else if (plussign && n > 0) {
        quotient_str.insert(0, '+');
    }

    if (num_decimals == 0) {
        return quotient_str;
    }

    const qint64 remainder = n_abs % coin;
    const QString remainder_str = QString::number(remainder).rightJustified(num_decimals, '0');
    return quotient_str + "." + remainder_str;
}

QmlBitcoinUnits::Unit QmlBitcoinUnits::fromDisplayUnit(int display_unit)
{
    switch (display_unit) {
    case 0: return Unit::BTC;
    case 1: return Unit::mBTC;
    case 2: return Unit::uBTC;
    case 3: return Unit::SAT;
    }
    return Unit::BTC;
}

QString QmlBitcoinUnits::label(Unit unit)
{
    switch (unit) {
    case Unit::BTC: return QStringLiteral("BTC");
    case Unit::mBTC: return QStringLiteral("mBTC");
    case Unit::uBTC: return QStringLiteral("bits");
    case Unit::SAT: return QStringLiteral("sat");
    }
    assert(false);
}

QString QmlBitcoinUnits::displayLabel(Unit unit, CAmount amount)
{
    switch (unit) {
    case Unit::BTC: return QStringLiteral("₿");
    case Unit::mBTC: return QStringLiteral("mBTC");
    case Unit::uBTC: return QStringLiteral("bits");
    case Unit::SAT:
        return qAbs(amount) == 1 ? QStringLiteral("sat") : QStringLiteral("sats");
    }
    assert(false);
}

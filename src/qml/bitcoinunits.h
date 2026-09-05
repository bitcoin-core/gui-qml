// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_BITCOINUNITS_H
#define BITCOIN_QML_BITCOINUNITS_H

#include <consensus/amount.h>

#include <QString>

class QmlBitcoinUnits
{
public:
    enum class Unit {
        BTC,
        mBTC,
        uBTC,
        SAT,
    };

    enum class SeparatorStyle {
        NEVER,
        STANDARD,
        ALWAYS,
    };

    static QString format(Unit unit, CAmount amount, bool plussign = false,
                          SeparatorStyle separators = SeparatorStyle::STANDARD);
    static Unit fromDisplayUnit(int display_unit);
    static QString label(Unit unit);
    static QString displayLabel(Unit unit, CAmount amount);

private:
    static qint64 factor(Unit unit);
    static int decimals(Unit unit);
};

#endif // BITCOIN_QML_BITCOINUNITS_H

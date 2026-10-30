// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/bitcoinunits.h>

#include <QByteArray>
#include <QApplication>
#include <QString>

#include <cstddef>
#include <cstdint>

static int argc = 1;
static char arg0[] = "fuzz_bitcoin_units";
static char* argv[] = {arg0, nullptr};
static QApplication app(argc, argv);

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (size < sizeof(qint64)) return 0;

    qint64 amount = 0;
    for (size_t i = 0; i < sizeof(qint64); ++i) {
        amount = (amount << 8) | data[i];
    }

    const int unit_idx = size > sizeof(qint64) ? data[sizeof(qint64)] % 4 : 0;
    const auto unit = static_cast<QmlBitcoinUnits::Unit>(unit_idx);

    (void)QmlBitcoinUnits::format(unit, amount);
    (void)QmlBitcoinUnits::formatForDisplay(unit, amount);
    (void)QmlBitcoinUnits::label(unit);
    (void)QmlBitcoinUnits::label(unit, amount);
    (void)QmlBitcoinUnits::displayLabel(unit, amount);
    return 0;
}

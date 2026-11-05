// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/bitcoinunits.h>

#include <QByteArray>
#include <QApplication>

#include <cstddef>
#include <cstdint>

static int argc = 1;
static char arg0[] = "fuzz_qml_bitcoinunits_extra";
static char* argv[] = {arg0, nullptr};
static QApplication app(argc, argv);

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (size == 0) return 0;

    for (size_t i = 0; i < size && i < 8; ++i) {
        (void)QmlBitcoinUnits::fromDisplayUnit(static_cast<int>(data[i]));
    }
    return 0;
}

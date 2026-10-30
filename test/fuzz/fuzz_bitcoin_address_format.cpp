// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/bitcoinaddress.h>

#include <QByteArray>
#include <QApplication>
#include <QString>

#include <cstddef>
#include <cstdint>

static int argc = 1;
static char arg0[] = "fuzz_bitcoin_address_format";
static char* argv[] = {arg0, nullptr};
static QApplication app(argc, argv);

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    const QByteArray raw(reinterpret_cast<const char*>(data), static_cast<int>(size));
    const QString text = QString::fromUtf8(raw);

    (void)BitcoinAddress::formattedAddress(text);
    (void)BitcoinAddress::ellipsesAddress(text);
    return 0;
}

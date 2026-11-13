// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/transaction.h>

#include <uint256.h>
#include <consensus/amount.h>

#include <QByteArray>
#include <QApplication>
#include <QString>

#include <cstddef>
#include <cstdint>

static int argc = 1;
static char arg0[] = "fuzz_transaction";
static char* argv[] = {arg0, nullptr};
static QApplication app(argc, argv);

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (size < 16) return 0;

    uint256 hash;
    for (size_t i = 0; i < 32 && i < size; ++i) hash.data()[i] = data[i];

    qint64 time = 0;
    for (size_t i = 0; i < 8 && (8 + i) < size; ++i) time = (time << 8) | data[8 + i];

    CAmount debit = 0, credit = 0;
    for (size_t i = 0; i < 8 && (16 + i) < size; ++i) debit = (debit << 8) | data[16 + i];
    for (size_t i = 0; i < 8 && (24 + i) < size; ++i) credit = (credit << 8) | data[24 + i];

    const auto type = static_cast<Transaction::Type>(size > 24 ? data[24] % 7 : 0);
    Transaction tx(hash, time, type, QString(), debit, credit);
    (void)tx.netAmount();
    (void)tx.prettyAmount(0);
    (void)tx.dateTimeString();
    (void)tx.timestamp;
    return 0;
}

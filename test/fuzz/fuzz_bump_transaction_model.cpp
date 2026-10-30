// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/bumptransactionmodel.h>

#include <chainparams.h>
#include <util/chaintype.h>

#include <QByteArray>
#include <QApplication>
#include <QString>

#include <cstddef>
#include <cstdint>

static int argc = 1;
static char arg0[] = "fuzz_bump_transaction_model";
static char* argv[] = {arg0, nullptr};
static QApplication app(argc, argv);

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    static bool params_selected = false;
    if (!params_selected) {
        SelectParams(ChainType::REGTEST);
        params_selected = true;
    }

    BumpTransactionModel model(/*wallet=*/nullptr);

    // First 4 bytes as target blocks, remainder as txid string.
    unsigned int target_blocks = 1;
    if (size >= sizeof(uint32_t)) {
        target_blocks = 0;
        for (size_t i = 0; i < sizeof(uint32_t); ++i) {
            target_blocks = (target_blocks << 8) | data[i];
        }
    }
    const QByteArray raw(reinterpret_cast<const char*>(data + sizeof(uint32_t)),
                         static_cast<int>(size > sizeof(uint32_t) ? size - sizeof(uint32_t) : 0));
    const QString txid = QString::fromUtf8(raw);

    model.prepareFeeBump(txid, target_blocks);
    (void)model.state();
    (void)model.actionType();
    (void)model.oldFee();
    (void)model.newFee();
    (void)model.feeIncrease();
    (void)model.oldTxid();
    (void)model.newTxid();
    (void)model.errorText();
    (void)model.needsUnlock();
    model.confirmFeeBump();
    model.confirmFeeBumpWithPassphrase(QStringLiteral("passphrase"));
    model.reset();
    return 0;
}

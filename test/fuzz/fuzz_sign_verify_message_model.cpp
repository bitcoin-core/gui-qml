// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/signverifymessagemodel.h>

#include <chainparams.h>
#include <util/chaintype.h>

#include <QByteArray>
#include <QApplication>
#include <QString>

#include <cstddef>
#include <cstdint>

static int argc = 1;
static char arg0[] = "fuzz_sign_verify_message_model";
static char* argv[] = {arg0, nullptr};
static QApplication app(argc, argv);

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    static bool params_selected = false;
    if (!params_selected) {
        SelectParams(ChainType::REGTEST);
        params_selected = true;
    }

    SignVerifyMessageModel model(/*wallet=*/nullptr);

    // Split input into null-terminated address/message/signature strings.
    QString address, message, signature;
    int field = 0;
    size_t start = 0;
    for (size_t i = 0; i <= size; ++i) {
        if (i == size || data[i] == '\0') {
            const QByteArray raw(reinterpret_cast<const char*>(data + start), static_cast<int>(i - start));
            const QString text = QString::fromUtf8(raw);
            switch (field % 3) {
            case 0: address = text; break;
            case 1: message = text; break;
            case 2: signature = text; break;
            }
            ++field;
            start = i + 1;
        }
    }

    (void)model.isLegacyP2PKHAddress(address);
    (void)model.verifyMessage(address, message, signature);
    model.signMessage(address, message);
    model.signMessageWithPassphrase(address, message, QStringLiteral("passphrase"));
    model.clear();
    model.clearSigningStatus();
    return 0;
}

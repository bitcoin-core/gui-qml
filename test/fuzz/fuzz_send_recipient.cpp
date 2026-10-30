// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/sendrecipient.h>

#include <chainparams.h>
#include <util/chaintype.h>

#include <QByteArray>
#include <QApplication>
#include <QString>

#include <cstddef>
#include <cstdint>

static int argc = 1;
static char arg0[] = "fuzz_send_recipient";
static char* argv[] = {arg0, nullptr};
static QApplication app(argc, argv);

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    static bool params_selected = false;
    if (!params_selected) {
        SelectParams(ChainType::REGTEST);
        params_selected = true;
    }

    SendRecipient recipient(/*wallet=*/nullptr);

    // First 8 bytes (if present) become the satoshi amount; remainder is split
    // into null-terminated strings for address/label/message/amount-display.
    size_t string_start = 0;
    if (size >= sizeof(int64_t)) {
        int64_t satoshi = 0;
        for (size_t i = 0; i < sizeof(int64_t); ++i) {
            satoshi = (satoshi << 8) | data[i];
        }
        recipient.amount()->setSatoshi(satoshi);
        string_start = sizeof(int64_t);
    }

    int field = 0;
    size_t start = string_start;
    for (size_t i = string_start; i <= size; ++i) {
        if (i == size || data[i] == '\0') {
            const QByteArray raw(reinterpret_cast<const char*>(data + start), static_cast<int>(i - start));
            const QString text = QString::fromUtf8(raw);
            switch (field % 4) {
            case 0:
                recipient.setAddress(text);
                break;
            case 1:
                recipient.setLabel(text);
                break;
            case 2:
                recipient.setMessage(text);
                break;
            case 3:
                recipient.amount()->fromDisplay(text);
                break;
            }
            ++field;
            start = i + 1;
        }
    }

    (void)recipient.isValid();
    (void)recipient.cAmount();
    (void)recipient.addressError();
    (void)recipient.amountError();
    recipient.applyPaymentRequest(recipient.address()->address(), QStringLiteral("pay-to"), QStringLiteral("message"));
    recipient.clear();
    return 0;
}

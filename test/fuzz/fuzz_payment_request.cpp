// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/paymentrequest.h>

#include <chainparams.h>
#include <key_io.h>
#include <util/chaintype.h>

#include <QByteArray>
#include <QApplication>
#include <QString>

#include <cstddef>
#include <cstdint>

static int argc = 1;
static char arg0[] = "fuzz_payment_request";
static char* argv[] = {arg0, nullptr};
static QApplication app(argc, argv);

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    static bool params_selected = false;
    if (!params_selected) {
        SelectParams(ChainType::MAIN);
        params_selected = true;
    }

    PaymentRequest request;

    // Treat the input as a sequence of null-terminated strings. Each string
    // drives a different property setter, exercising the URI-building path.
    size_t start = 0;
    int field = 0;
    for (size_t i = 0; i <= size; ++i) {
        if (i == size || data[i] == '\0') {
            const QByteArray raw(reinterpret_cast<const char*>(data + start), static_cast<int>(i - start));
            const QString text = QString::fromUtf8(raw);
            switch (field % 6) {
            case 0:
                request.setLabel(text);
                break;
            case 1:
                request.setMessage(text);
                break;
            case 2:
                request.setAddressType(text);
                break;
            case 3:
                request.setNoteSelf(text);
                break;
            case 4: {
                // Try to set a valid destination from a decoded address.
                const CTxDestination dest = DecodeDestination(text.toStdString());
                if (!IsValidDestination(dest)) break;
                request.setDestination(dest);
                break;
            }
            case 5:
                // Interpret the first 8 bytes as a satoshi amount.
                if (raw.size() >= static_cast<int>(sizeof(int64_t))) {
                    int64_t satoshi = 0;
                    for (size_t j = 0; j < sizeof(int64_t); ++j) {
                        satoshi = (satoshi << 8) | static_cast<uint8_t>(raw.at(j));
                    }
                    request.amount()->setSatoshi(satoshi);
                }
                break;
            }
            ++field;
            start = i + 1;
        }
    }

    (void)request.address();
    (void)request.addressFormatted();
    (void)request.addressType();
    (void)request.qrPayload();
    (void)request.hasPaymentInfo();
    (void)request.createdIso();
    request.edit();
    request.clear();
    return 0;
}

// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/paymentrequest.h>

#include <chainparams.h>
#include <key_io.h>
#include <util/chaintype.h>

#include <QByteArray>
#include <QApplication>
#include <QDateTime>
#include <QString>

#include <cstddef>
#include <cstdint>

static int argc = 1;
static char arg0[] = "fuzz_payment_request_deserialization";
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

    size_t start = 0;
    int field = 0;
    for (size_t i = 0; i <= size; ++i) {
        if (i == size || data[i] == '\0') {
            const QByteArray raw(reinterpret_cast<const char*>(data + start), static_cast<int>(i - start));
            const QString text = QString::fromUtf8(raw);
            switch (field % 12) {
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
                const CTxDestination dest = DecodeDestination(text.toStdString());
                if (IsValidDestination(dest)) request.setDestination(dest);
                break;
            }
            case 5:
                if (raw.size() >= static_cast<int>(sizeof(int64_t))) {
                    int64_t satoshi = 0;
                    for (size_t j = 0; j < sizeof(int64_t); ++j) {
                        satoshi = (satoshi << 8) | static_cast<uint8_t>(raw.at(j));
                    }
                    request.amount()->setSatoshi(satoshi);
                }
                break;
            case 6:
                request.setAmountError(text);
                break;
            case 7:
                request.setUnlockError(text);
                break;
            case 8:
                request.setNeedsUnlock(size > 0 && (data[0] & 1));
                break;
            case 9:
                if (!text.isEmpty()) {
                    bool ok = false;
                    const unsigned int id = text.toUInt(&ok);
                    if (ok) request.setId(id);
                }
                break;
            case 10:
                request.setCreated(QDateTime::fromString(text, Qt::ISODate));
                break;
            case 11:
                request.setIsEditing(size > 0 && (data[0] & 2));
                request.setPaymentReceived(size > 0 && (data[0] & 4));
                if (raw.size() >= static_cast<int>(sizeof(qint64))) {
                    qint64 amt = 0;
                    for (size_t j = 0; j < sizeof(qint64); ++j) {
                        amt = (amt << 8) | static_cast<uint8_t>(raw.at(j));
                    }
                    request.setReceivedAmountSatoshi(amt);
                }
                break;
            }
            ++field;
            start = i + 1;
        }
    }

    (void)request.address();
    (void)request.addressFormatted();
    (void)request.qrPayload();
    (void)request.hasPaymentInfo();
    (void)request.createdIso();
    (void)request.isEditing();
    (void)request.paymentReceived();
    (void)request.receivedAmountSatoshi();
    request.edit();
    request.clear();
    return 0;
}

// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_RECEIVEREQUESTENTRY_H
#define BITCOIN_QML_MODELS_RECEIVEREQUESTENTRY_H

#include <consensus/amount.h>
#include <serialize.h>

#include <cstdint>
#include <string>

#include <QDateTime>

struct QmlReceiveRequestRecipient
{
    static constexpr int CURRENT_VERSION{2};
    int nVersion{CURRENT_VERSION};
    std::string address;
    std::string label;
    CAmount amount{0};
    std::string message;
    std::string sPaymentRequest;
    std::string authenticatedMerchant;
    std::string noteSelf;

    SERIALIZE_METHODS(QmlReceiveRequestRecipient, obj)
    {
        READWRITE(obj.nVersion, obj.address, obj.label, obj.amount, obj.message, obj.sPaymentRequest, obj.authenticatedMerchant);
        if (obj.nVersion >= 2) {
            READWRITE(obj.noteSelf);
        }
    }
};

struct QmlRecentRequestEntry
{
    static constexpr int CURRENT_VERSION{1};
    int nVersion{CURRENT_VERSION};
    int64_t id{0};
    QDateTime date;
    QmlReceiveRequestRecipient recipient;

    SERIALIZE_METHODS(QmlRecentRequestEntry, obj)
    {
        int64_t date_timet;
        SER_WRITE(obj, date_timet = obj.date.toSecsSinceEpoch());
        READWRITE(obj.nVersion, obj.id, date_timet, obj.recipient);
        SER_READ(obj, obj.date = QDateTime::fromSecsSinceEpoch(date_timet));
    }
};

#endif // BITCOIN_QML_MODELS_RECEIVEREQUESTENTRY_H

// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_BITCOINURI_H
#define BITCOIN_QML_MODELS_BITCOINURI_H

#include <consensus/amount.h>

#include <QString>

struct BitcoinUriParseResult
{
    bool success{false};
    QString error;
    QString address;
    CAmount amount_sats{0};
    bool has_amount{false};
    QString label;
    bool has_label{false};
    QString message;
    bool has_message{false};
};

class BitcoinUri
{
public:
    static BitcoinUriParseResult Parse(const QString& uri_text);
};

#endif // BITCOIN_QML_MODELS_BITCOINURI_H

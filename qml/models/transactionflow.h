// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_TRANSACTIONFLOW_H
#define BITCOIN_QML_MODELS_TRANSACTIONFLOW_H

#include <primitives/transaction.h>

#include <QVariantMap>

#include <optional>
#include <vector>

namespace interfaces { struct WalletTx; }

// Complete input/output data, independent of list actions and presentation.
// prevouts is positional and may contain unknown entries. It can also be
// supplied from a prepared transaction/PSBT when building a send preview.
QVariantMap BuildTransactionFlow(const interfaces::WalletTx& tx,
                               const std::vector<std::optional<CTxOut>>& prevouts);

#endif // BITCOIN_QML_MODELS_TRANSACTIONFLOW_H

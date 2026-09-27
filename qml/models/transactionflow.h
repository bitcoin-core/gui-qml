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

// Input/output data, independent of list actions.
// prevouts is positional and may contain unknown entries. It can also be
// supplied from a prepared transaction/PSBT when building a send preview.
// A collapsed preview keeps wallet outputs and totals the other outputs without
// decoding their addresses or data payloads. The original output count is kept.
QVariantMap BuildTransactionFlow(const interfaces::WalletTx& tx,
                               const std::vector<std::optional<CTxOut>>& prevouts,
                               bool collapse_outputs = false);

#endif // BITCOIN_QML_MODELS_TRANSACTIONFLOW_H

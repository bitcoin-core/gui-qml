// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_TRANSACTIONACTIVITY_H
#define BITCOIN_QML_MODELS_TRANSACTIONACTIVITY_H

#include <consensus/amount.h>
#include <primitives/transaction.h>

#include <optional>
#include <vector>

#include <QString>

namespace interfaces {
struct WalletTx;
}

struct TransactionAction
{
    enum class Direction { Send, Receive, Internal };
    enum class Source { Output, WalletInputs };

    // Stable within a transaction, independent of labels and confirmation state.
    QString id;
    Direction direction{Direction::Internal};
    Source source{Source::Output};
    // Unsigned magnitude in satoshis: the output value or wallet input total.
    // No transaction fee is added to an action's amount.
    CAmount amount{0};
    QString address;
    int output_index{-1};
    // A mixed-input contribution is an aggregate, not a payment to an external
    // output. WalletTx provides its total debit but no per-input values.
    std::vector<COutPoint> inputs;
};

/** Transaction-level activity, before labels, requests, status and filtering.
 *
 * The interpreter is independent of QML model and view state.
 * Interpret WalletTx directly: those records have already lost mixed-input
 * details and allocated fees to individual outputs.
 */
struct TransactionActivity
{
    enum class Type { Send, Receive, Multiple, Consolidation, Split, InternalTransfer, Mined, Other };

    QString txid;
    Type type{Type::Other};
    qint64 timestamp{0};
    CAmount wallet_debit{0};
    CAmount wallet_credit{0};
    // The total network fee is known here only when all inputs belong to the
    // wallet. nullopt is distinct from a known zero fee and from wallet impact.
    std::optional<CAmount> fee;
    QString replaces_txid;
    QString replaced_by_txid;
    std::vector<TransactionAction> actions;

    // Includes the wallet's fee impact. Do not add fee again or sum actions:
    // internal outputs move existing funds, and mixed receipts include inputs
    // contributed by this wallet as well as any new funds.
    CAmount walletImpact() const { return wallet_credit - wallet_debit; }

    // Returns no activity for a missing/incomplete snapshot or a transaction
    // with no wallet-owned inputs or outputs.
    static std::optional<TransactionActivity> fromWalletTx(const interfaces::WalletTx& wtx);
};

#endif // BITCOIN_QML_MODELS_TRANSACTIONACTIVITY_H

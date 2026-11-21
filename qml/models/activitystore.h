// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_ACTIVITYSTORE_H
#define BITCOIN_QML_MODELS_ACTIVITYSTORE_H

#include <qml/models/transactionactivity.h>
#include <qml/models/transaction.h>

#include <consensus/amount.h>
#include <interfaces/wallet.h>
#include <primitives/transaction.h>

#include <QMap>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <atomic>
#include <cstdint>
#include <optional>
#include <vector>

namespace activitystore {

/** Independent input revisions for the wallet observations used by Activity.
 * Presentation-only changes (such as display units) do not advance these. */
struct Revisions {
    quint64 history{0};
    quint64 transactions{0};
    quint64 labels{0};
    quint64 status{0};
    quint64 details{0};
    bool operator==(const Revisions&) const = default;
};

/** Work is coalesced by observation lane. Transaction keys remain bounded by
 * the consumer; overflow requests a complete history read. */
struct Work {
    bool reload{false}, labels{false}, statuses{false}, poll{false}, details{false}, projection{false};
    QSet<QString> transactions;
    Revisions revisions;
    bool empty() const { return !reload && !labels && !statuses && !poll && !details && !projection && transactions.isEmpty(); }
    void merge(const Work& other);
    bool supersededBy(const Revisions& latest) const;
};

struct Record {
    TransactionActivity activity;
    QStringList labels;
    int status{Transaction::Unconfirmed};
    int depth{0};
    int blocks_to_maturity{0};
    int block_height{0};
    bool status_known{false};
    // Core's current mempool membership for a depth-zero transaction.
    bool in_mempool{false};
    quint64 transaction_version{0}, labels_version{0}, status_version{0};
    CTransactionRef transaction;
    // Compact ownership facts support a cached Domain preview without another
    // wallet read or address-book scan.
    std::vector<bool> inputs_mine, outputs_mine, outputs_change;
};

/** Immutable Core-derived store frame. The revision vector identifies exactly
 * which observations were captured; a GUI consumer must not call Core to
 * reconcile it. */
struct Snapshot {
    QMap<QString, Record> records;
    QSet<QString> retry;
    std::optional<uint256> tip;
    QString detail_txid, raw_transaction;
    QVariantMap flow;
    QMap<QString, QString> detail_labels;
    Revisions revisions;
    quint64 transaction_revision{0};
    bool history_loaded{false};
    bool can_bump{false}, detail_missing{false}, rows_changed{false}, details_changed{false}, flow_resolved{false};
    bool complete{true};
};

/** Public-interface reader. It has no QObject or GUI dependency; the caller
 * runs it on the opening's Core lane and retains its immutable result on the
 * Domain lane. */
class Reader final {
public:
    static Snapshot read(interfaces::Wallet& wallet, Snapshot snapshot, const Work& work,
                         const std::atomic_bool& cancelled, quint64 transaction_revision);
    static Transaction::Status status(const TransactionActivity& activity, const interfaces::WalletTxStatus& value);
private:
    static bool updateStatus(interfaces::Wallet& wallet, Record& record);
    static void updateLabels(interfaces::Wallet& wallet, Record& record);
};

} // namespace activitystore

#endif // BITCOIN_QML_MODELS_ACTIVITYSTORE_H

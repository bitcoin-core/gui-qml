// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/activitystore.h>

#include <qml/models/transactionflow.h>

#include <core_io.h>
#include <key_io.h>

#include <algorithm>
#include <limits>

namespace activitystore {
namespace {
using Type = TransactionActivity::Type;

QString AddressLabel(interfaces::Wallet& wallet, const QString& address)
{
    if (address.isEmpty()) return {};
    const auto destination = DecodeDestination(address.toStdString());
    if (!IsValidDestination(destination)) return {};
    std::string label;
    if (wallet.getAddress(destination, &label, nullptr)) return QString::fromStdString(label);
    return {};
}

void ApplyReadRevisions(Snapshot& snapshot, const Work& work)
{
    if (work.reload) {
        snapshot.revisions.history = work.revisions.history;
        snapshot.revisions.transactions = work.revisions.transactions;
        snapshot.revisions.labels = work.revisions.labels;
        snapshot.revisions.status = work.revisions.status;
    } else {
        if (!work.transactions.isEmpty()) snapshot.revisions.transactions = work.revisions.transactions;
        if (work.labels) snapshot.revisions.labels = work.revisions.labels;
        if (work.statuses || work.poll) snapshot.revisions.status = work.revisions.status;
    }
    if (work.details) snapshot.revisions.details = work.revisions.details;
}
} // namespace

void Work::merge(const Work& other)
{
    reload |= other.reload;
    labels |= other.labels;
    statuses |= other.statuses;
    poll |= other.poll;
    details |= other.details;
    projection |= other.projection;
    transactions.unite(other.transactions);
    if (transactions.size() > 256) {
        transactions.clear();
        reload = true;
    }
    if (reload || !transactions.isEmpty()) {
        revisions.history = std::max(revisions.history, other.revisions.history);
        revisions.transactions = std::max(revisions.transactions, other.revisions.transactions);
        revisions.labels = std::max(revisions.labels, other.revisions.labels);
        revisions.status = std::max(revisions.status, other.revisions.status);
    }
    if (other.labels) revisions.labels = std::max(revisions.labels, other.revisions.labels);
    if (other.statuses || other.poll) revisions.status = std::max(revisions.status, other.revisions.status);
    if (other.details) revisions.details = std::max(revisions.details, other.revisions.details);
}

bool Work::supersededBy(const Revisions& latest) const
{
    const bool reads_activity = reload || !transactions.isEmpty() || labels || statuses || poll || details;
    if (reads_activity && revisions.history != latest.history) return true;
    if (reload && (revisions.transactions != latest.transactions || revisions.labels != latest.labels ||
                   revisions.status != latest.status)) return true;
    if (!transactions.isEmpty() && (revisions.transactions != latest.transactions || revisions.labels != latest.labels ||
                                    revisions.status != latest.status)) return true;
    if (labels && (revisions.transactions != latest.transactions || revisions.labels != latest.labels)) return true;
    if ((statuses || poll) && (revisions.transactions != latest.transactions || revisions.status != latest.status)) return true;
    // Detail selection has its own generation and is checked by the detail
    // consumer. A selection change must not discard useful history work.
    return false;
}

Transaction::Status Reader::status(const TransactionActivity& activity, const interfaces::WalletTxStatus& value)
{
    // Keep this predicate in one place, but preserve Activity's existing
    // precedence: a generated transaction outside the active chain is not
    // accepted; ordinary transactions prioritize conflict over abandonment.
    if (activity.type == Type::Mined) {
        return !value.is_in_main_chain ? Transaction::NotAccepted
             : value.blocks_to_maturity > 0 ? Transaction::Immature : Transaction::Confirmed;
    }
    return value.depth_in_main_chain < 0 ? Transaction::Conflicted
         : value.is_abandoned ? Transaction::Abandoned
         : value.depth_in_main_chain == 0 ? Transaction::Unconfirmed
         : value.depth_in_main_chain < 6 ? Transaction::Confirming : Transaction::Confirmed;
}

bool Reader::updateStatus(interfaces::Wallet& wallet, Record& record)
{
    interfaces::WalletTxStatus status{};
    int blocks{0};
    int64_t block_time{0};
    const uint256 txid{uint256::FromHex(record.activity.txid.toStdString()).value()};
    bool in_mempool{false};
    // A busy wallet is not a missing transaction. Waiting for its public
    // detail read is safe here on the original opening's Native lane and
    // avoids postponing a valid status until another polling deadline. A
    // depth-zero transaction also needs Core's mempool membership (#1072).
    if (!wallet.tryGetTxStatus(Txid::FromUint256(txid), status, blocks, block_time) || status.depth_in_main_chain == 0) {
        std::vector<std::string> messages, payment_requests;
        if (!wallet.getWalletTxDetails(Txid::FromUint256(txid), status, messages, payment_requests, in_mempool, blocks).tx) return false;
    }

    record.status_known = true;
    record.in_mempool = in_mempool;
    record.depth = status.depth_in_main_chain;
    record.blocks_to_maturity = status.blocks_to_maturity;
    record.block_height = record.depth > 0 ? status.block_height : 0;
    record.status = Reader::status(record.activity, status);
    return true;
}

void Reader::updateLabels(interfaces::Wallet& wallet, Record& record)
{
    record.labels.clear();
    for (const auto& action : record.activity.actions) record.labels.append(AddressLabel(wallet, action.address));
}

Snapshot Reader::read(interfaces::Wallet& wallet, Snapshot snapshot, const Work& work,
                      const std::atomic_bool& cancelled, quint64 transaction_revision,
                      std::chrono::steady_clock::time_point now)
{
    snapshot.rows_changed = snapshot.details_changed = false;
    bool status_failed{false};
    const auto read_record = [&](const interfaces::WalletTx& wtx) {
        auto activity = TransactionActivity::fromWalletTx(wtx);
        if (!activity) return;
        Record record = snapshot.records.value(activity->txid);
        record.activity = std::move(*activity);
        record.transaction = wtx.tx;
        record.transaction_version = transaction_revision;
        record.inputs_mine = wtx.txin_is_mine;
        record.outputs_mine = wtx.txout_is_mine;
        record.outputs_change = wtx.txout_is_change;
        updateLabels(wallet, record);
        record.labels_version = transaction_revision;
        if (updateStatus(wallet, record)) {
            record.status_version = transaction_revision;
            snapshot.retry.remove(record.activity.txid);
        } else {
            snapshot.retry.insert(record.activity.txid);
            status_failed = true;
        }
        snapshot.records.insert(record.activity.txid, record);
    };
    if (work.reload) {
        QSet<QString> present;
        for (const auto& wtx : wallet.getWalletTxs()) {
            if (cancelled.load()) return snapshot;
            if (wtx.tx) present.insert(QString::fromStdString(wtx.tx->GetHash().ToString()));
            read_record(wtx);
        }
        for (auto it = snapshot.records.begin(); it != snapshot.records.end();) {
            if (!present.contains(it.key())) {
                snapshot.retry.remove(it.key());
                it = snapshot.records.erase(it);
            } else {
                ++it;
            }
        }
        snapshot.history_loaded = true;
    }
    for (const auto& txid : work.transactions) {
        if (cancelled.load()) return snapshot;
        const auto hash = uint256::FromHex(txid.toStdString());
        if (!hash) continue;
        const auto tx = wallet.getWalletTx(Txid::FromUint256(*hash));
        if (tx.tx) read_record(tx);
        else { snapshot.records.remove(txid); snapshot.retry.remove(txid); }
    }
    if (work.labels) {
        for (auto& record : snapshot.records) {
            if (cancelled.load()) return snapshot;
            updateLabels(wallet, record);
            record.labels_version = transaction_revision;
        }
    }
    bool statuses = work.statuses;
    if (work.poll) {
        interfaces::WalletBalances balances{};
        uint256 tip;
        if (wallet.tryGetBalances(balances, tip) && (!snapshot.tip || *snapshot.tip != tip)) {
            snapshot.tip = tip;
            statuses = true;
        }
    }
    const bool retry = work.poll && !snapshot.retry.isEmpty() && now >= snapshot.retry_after;
    const auto refresh = [&](Record& record) {
        if (updateStatus(wallet, record)) {
            record.status_version = transaction_revision;
            snapshot.retry.remove(record.activity.txid);
        } else {
            snapshot.retry.insert(record.activity.txid);
            status_failed = true;
        }
    };
    if (statuses) {
        for (auto& record : snapshot.records) {
            if (cancelled.load()) return snapshot;
            refresh(record);
        }
    } else if (retry) {
        for (const auto& txid : QSet<QString>{snapshot.retry}) {
            if (cancelled.load()) return snapshot;
            const auto found = snapshot.records.find(txid);
            if (found == snapshot.records.end()) snapshot.retry.remove(txid);
            else refresh(*found);
        }
    }
    // A transaction in the mempool leaves it without a block or a wallet
    // notification when it expires or is evicted (#1072). Each poll rechecks
    // the membership of those records; a change reprojects the rows.
    bool left_mempool{false};
    if (work.poll && !statuses) {
        for (auto& record : snapshot.records) {
            if (cancelled.load()) return snapshot;
            if (!record.status_known || record.depth != 0 || !record.in_mempool) continue;
            refresh(record);
            left_mempool |= !record.in_mempool || record.depth != 0;
        }
    }
    // Space out retries of a persistently failing status read, as the opening
    // refresh does; any complete success resets the backoff.
    if (status_failed) {
        snapshot.retry_after = now + std::chrono::seconds{std::min(30U, 1U << snapshot.retry_failures)};
        snapshot.retry_failures = std::min(5U, snapshot.retry_failures + 1);
    } else if (snapshot.retry.isEmpty()) {
        snapshot.retry_failures = 0;
        snapshot.retry_after = {};
    }
    snapshot.rows_changed = work.reload || work.labels || statuses || retry || left_mempool || !work.transactions.isEmpty();
    snapshot.details_changed = work.details || snapshot.rows_changed;
    if (snapshot.detail_txid.isEmpty() || !snapshot.details_changed || cancelled.load()) {
        ApplyReadRevisions(snapshot, work);
        snapshot.transaction_revision = transaction_revision;
        return snapshot;
    }

    // A send-result link may arrive before its queued transaction notification.
    const auto hash = uint256::FromHex(snapshot.detail_txid.toStdString());
    if (hash && !snapshot.records.contains(snapshot.detail_txid)) {
        read_record(wallet.getWalletTx(Txid::FromUint256(*hash)));
        snapshot.rows_changed = true;
    }
    snapshot.detail_missing = !hash || !snapshot.records.contains(snapshot.detail_txid);
    snapshot.can_bump = false;
    if (snapshot.detail_missing) {
        snapshot.flow.clear();
        ApplyReadRevisions(snapshot, work);
        snapshot.transaction_revision = transaction_revision;
        return snapshot;
    }
    if (!snapshot.flow_resolved || snapshot.flow.isEmpty() || work.reload || !work.transactions.isEmpty()) {
        const auto wtx = wallet.getWalletTx(Txid::FromUint256(*hash));
        if (!wtx.tx) {
            snapshot.detail_missing = true;
            snapshot.flow.clear();
            ApplyReadRevisions(snapshot, work);
            snapshot.transaction_revision = transaction_revision;
            return snapshot;
        }
        std::map<Txid, CTransactionRef> parents;
        std::vector<std::optional<CTxOut>> prevouts;
        if (!wtx.tx->IsCoinBase()) {
            for (const auto& input : wtx.tx->vin) {
                if (cancelled.load()) return snapshot;
                auto [it, inserted] = parents.try_emplace(input.prevout.hash);
                if (inserted) it->second = wallet.getTx(input.prevout.hash);
                prevouts.push_back(it->second && input.prevout.n < it->second->vout.size()
                    ? std::optional{it->second->vout[input.prevout.n]} : std::nullopt);
            }
        }
        snapshot.flow = BuildTransactionFlow(wtx, prevouts);
        snapshot.flow_resolved = true;
        snapshot.raw_transaction = QString::fromStdString(EncodeHexTx(*wtx.tx));
        snapshot.detail_labels.clear();
    }
    if (work.labels) snapshot.detail_labels.clear();
    for (const QString& side : {QStringLiteral("inputs"), QStringLiteral("outputs")}) {
        for (const auto& value : snapshot.flow.value(side).toList()) {
            if (cancelled.load()) return snapshot;
            const auto address = value.toMap().value("address").toString();
            if (!snapshot.detail_labels.contains(address)) snapshot.detail_labels.insert(address, AddressLabel(wallet, address));
        }
    }
    const auto record_it = snapshot.records.constFind(snapshot.detail_txid);
    const auto& record = record_it.value();
    if (record.status_known && record.status == Transaction::Unconfirmed && record.activity.replaced_by_txid.isEmpty()) {
        snapshot.can_bump = wallet.transactionCanBeBumped(Txid::FromUint256(*hash));
    }
    ApplyReadRevisions(snapshot, work);
    snapshot.transaction_revision = transaction_revision;
    return snapshot;
}

} // namespace activitystore

// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_WALLETRECEIPTS_H
#define BITCOIN_QML_WALLETRECEIPTS_H

#include <qml/walletopening.h>
#include <qml/observationretirement.h>

#include <QSet>
#include <QString>

#include <atomic>
#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <vector>

namespace walletreceipts {
struct Receipt {
    Txid id;
    std::map<QString, CAmount> outputs;
    bool active{false}, unconfirmed{false};
};
Receipt Read(interfaces::Wallet& wallet, const Txid& id);

struct ReceiptIdNode;
struct RecheckSnapshot;
/** Immutable ordered receipt IDs. Capturing a recheck interest shares a root;
 * individual status changes and cursor lookups visit logarithmic paths.
 * Domain changes nodes; retained recheck roots retire on Cleanup. */
class ReceiptIds
{
public:
    bool empty() const { return !m_root; }
    size_t size() const;
    bool contains(const Txid& id) const;
    void insert(const Txid& id);
    void erase(const Txid& id);
    void clear() { m_root.reset(); }
    std::optional<Txid> next(const std::optional<Txid>& after) const;
private:
    std::shared_ptr<const ReceiptIdNode> m_root;
};

/** Deterministic receipt ledger. It is owned and changed on Domain. Observing
 * an owned output permanently retires its address, even after a conflict. */
class Ledger
{
public:
    void apply(const Receipt& receipt, const QSet<QString>& addresses);
    void erase(const Txid& id);
    void clearPayments();
    bool emptyPayments() const { return m_payments.empty() && m_unconfirmed.empty(); }
    // Native value transfer after Cleanup has been reserved. Permanently
    // observed addresses stay in this original ledger.
    std::unique_ptr<Ledger> takePayments();
    const std::map<QString, CAmount>& totals() const { return m_totals; }
    const std::set<QString>& observed() const { return m_observed; }
    const ReceiptIds& unconfirmed() const { return m_unconfirmed; }
private:
    std::map<Txid, std::map<QString, CAmount>> m_payments;
    std::map<QString, CAmount> m_totals;
    std::set<QString> m_observed;
    ReceiptIds m_unconfirmed;
};

enum class Status { Ready, Pending, Failed, Rejected, Closed };
struct Snapshot {
    walletopening::Opening opening;
    std::map<QString, CAmount> totals;
    std::set<QString> observed;
    uint64_t native_revision{0};
    QSet<QString> addresses{};
};
struct Publication {
    std::shared_ptr<const Snapshot> snapshot;
    std::map<Txid, ChangeType> transactions;
    Status status{Status::Ready};
    bool needs_poll{false}, rescan{false};
    uint64_t revision{0};
};

/** One receipt observation owner per opening. Core reads and ledger decisions
 * have separate injected turns; GUI only consumes immutable snapshots.
 * Native mutations are deliberately not performed by this read port. */
class Reader : public std::enable_shared_from_this<Reader>
{
public:
    using Notify = std::function<void()>;
    using Clock = walletopening::Session::Clock;
    using Consumer = std::function<void(std::shared_ptr<const Snapshot>)>;
    Reader(std::shared_ptr<walletopening::Session> opening, Notify notify,
           Clock clock = std::chrono::steady_clock::now);
    void reload(QSet<QString> addresses, bool rescan = false);
    void changed(std::map<Txid, ChangeType> changes, QSet<QString> addresses);
    void notice(uint64_t revision, std::map<Txid, ChangeType> changes, bool rescan);
    void poll();
    void retry();
    void close();
    // Backend readiness and receipt delivery never wait for GUI acknowledgement.
    bool settled() const;
    execution::SubmitResult onSnapshot(Consumer consumer);
    std::optional<Publication> take();
    // Schedule an independent Domain turn, without a new receipt read. Rejected
    // posts retain references for retry or the owner's background retirement.
    void retire(std::shared_ptr<const Snapshot> snapshot);
private:
    struct Work;
    void schedule();
    void plan();
    struct Scan;
    void beginScan(Work work, std::set<interfaces::WalletTx> transactions);
    void scanNext(std::shared_ptr<Scan> scan);
    void readChanges(Work work);
    void complete(std::vector<Receipt> receipts, std::map<Txid, ChangeType> changes,
                  QSet<QString> addresses, bool rescan, uint64_t revision, uint64_t native_revision);
    void failed(Status status);
    void publish(Publication update);
    struct Work {
        QSet<QString> addresses;
        std::map<Txid, ChangeType> changes;
        std::set<Txid> statuses;
        bool full{false};
        bool rescan{false};
        uint64_t revision{0};
        uint64_t native_revision{0};
    };
    const std::shared_ptr<walletopening::Session> m_opening;
    const Notify m_notify;
    const Clock m_clock;
    const execution::ObservationRetirement<Snapshot> m_retirement;
    std::atomic_bool m_settled{false};
    std::atomic<uint64_t> m_settled_native_revision{0};
    Ledger m_ledger; // Domain only
    // A new notice captures an immutable root, without copying the inventory.
    // Only its already-read bounded batch is skipped. Thus prior reads never
    // suppress a later replacement check.
    bool restartRechecks();
    bool clearPayments();
    std::optional<Txid> nextRecheck();
    std::shared_ptr<const RecheckSnapshot> m_recheck; // Mutex-protected alias; Cleanup owns root retirement.
    const std::shared_ptr<std::atomic_uint> m_recheck_inventory{std::make_shared<std::atomic_uint>(0)};
    std::optional<Txid> m_recheck_after;
    std::set<Txid> m_recheck_skip; // At most 256 changes + 128 status reads.
    std::mutex m_mutex;
    QSet<QString> m_addresses;
    std::map<Txid, ChangeType> m_changes;
    bool m_full{false}, m_rescan{false}, m_poll{false}, m_scheduled{false}, m_active{false}, m_followup{false};
    std::map<Txid, ChangeType> m_inflight;
    std::map<Txid, ChangeType> m_announcements; // Domain only
    bool m_has_unconfirmed{false};
    bool m_closed{false}, m_notice_pending{false};
    std::chrono::steady_clock::time_point m_retry_after{};
    unsigned m_failures{0};
    uint64_t m_revision{0};
    uint64_t m_native_revision{0};
    Consumer m_consumer;
    std::optional<Publication> m_latest;
    std::shared_ptr<const Snapshot> m_current;
};
} // namespace walletreceipts

#endif // BITCOIN_QML_WALLETRECEIPTS_H

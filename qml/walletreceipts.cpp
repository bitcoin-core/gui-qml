// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/walletreceipts.h>
#include <qml/retainedvalue.h>

#include <key_io.h>
#include <script/solver.h>

#include <algorithm>
#include <utility>

namespace walletreceipts {
struct ReceiptIdNode {
    Txid id;
    std::shared_ptr<const ReceiptIdNode> left, right;
    int height{1};
    size_t size{1};
};
struct RecheckSnapshot {
    // Members release in reverse order: the slot is refunded only after the
    // actual immutable tree allocation has been released on Cleanup.
    std::shared_ptr<void> inventory;
    ReceiptIds ids;
};
struct RetiredPayments {
    std::shared_ptr<void> inventory;
    std::unique_ptr<Ledger> ledger;
};
namespace {
struct RecheckSlot {
    explicit RecheckSlot(std::shared_ptr<std::atomic_uint> count) : count(std::move(count))
    {
        auto live = this->count->load();
        while (live < 2) {
            if (this->count->compare_exchange_strong(live, live + 1)) {
                admitted = true;
                break;
            }
        }
    }
    ~RecheckSlot() { if (admitted) count->fetch_sub(1); }
    const std::shared_ptr<std::atomic_uint> count;
    bool admitted{false};
};
using IdTree = std::shared_ptr<const ReceiptIdNode>;
int Height(const IdTree& tree) { return tree ? tree->height : 0; }
size_t Size(const IdTree& tree) { return tree ? tree->size : 0; }
IdTree Make(const Txid& id, IdTree left = {}, IdTree right = {})
{
    auto node = std::make_shared<ReceiptIdNode>();
    node->id = id;
    node->left = std::move(left);
    node->right = std::move(right);
    node->height = 1 + std::max(Height(node->left), Height(node->right));
    node->size = 1 + Size(node->left) + Size(node->right);
    return node;
}
IdTree RotateLeft(const IdTree& tree)
{
    const auto& child = tree->right;
    return Make(child->id, Make(tree->id, tree->left, child->left), child->right);
}
IdTree RotateRight(const IdTree& tree)
{
    const auto& child = tree->left;
    return Make(child->id, child->left, Make(tree->id, child->right, tree->right));
}
IdTree Balance(IdTree tree)
{
    const auto delta = Height(tree->left) - Height(tree->right);
    if (delta > 1) {
        if (Height(tree->left->right) > Height(tree->left->left)) tree = Make(tree->id, RotateLeft(tree->left), tree->right);
        return RotateRight(tree);
    }
    if (delta < -1) {
        if (Height(tree->right->left) > Height(tree->right->right)) tree = Make(tree->id, tree->left, RotateRight(tree->right));
        return RotateLeft(tree);
    }
    return tree;
}
struct IdAncestor { const ReceiptIdNode* node; bool left; };
IdTree Rebuild(const std::vector<IdAncestor>& path, IdTree result)
{
    for (auto it = path.rbegin(); it != path.rend(); ++it) {
        const auto* node = it->node;
        result = Balance(it->left ? Make(node->id, std::move(result), node->right)
            : Make(node->id, node->left, std::move(result)));
    }
    return result;
}
Receipt Capture(const interfaces::WalletTx& tx, const interfaces::WalletTx& current, const interfaces::WalletTxStatus& status, bool mempool)
{
    Receipt result;
    if (!tx.tx) return result;
    result.id = tx.tx->GetHash();
    for (size_t i{0}; i < tx.tx->vout.size(); ++i) {
        const auto& output = tx.tx->vout[i];
        if (output.nValue <= 0 || i >= tx.txout_is_mine.size() || !tx.txout_is_mine[i]) continue;
        CTxDestination destination;
        if (ExtractDestination(output.scriptPubKey, destination)) {
            result.outputs[QString::fromStdString(EncodeDestination(destination))] += output.nValue;
        }
    }
    if (result.outputs.empty()) return result;
    // Mempool membership is current; a replacement marker is history that a
    // reorg can outlive, returning the original to the mempool (#1072).
    result.active = current.tx && status.depth_in_main_chain >= 0 && !status.is_abandoned &&
        (status.depth_in_main_chain > 0 || mempool);
    result.unconfirmed = result.active && status.depth_in_main_chain == 0;
    return result;
}
} // namespace

size_t ReceiptIds::size() const { return Size(m_root); }

bool ReceiptIds::contains(const Txid& id) const
{
    auto* node = m_root.get();
    while (node && node->id != id) node = (id < node->id ? node->left : node->right).get();
    return node != nullptr;
}

void ReceiptIds::insert(const Txid& id)
{
    std::vector<IdAncestor> path;
    path.reserve(Height(m_root));
    auto* node = m_root.get();
    while (node && node->id != id) {
        const bool left = id < node->id;
        path.push_back({node, left});
        node = (left ? node->left : node->right).get();
    }
    if (node) return;
    m_root = Rebuild(path, Make(id));
}

void ReceiptIds::erase(const Txid& id)
{
    std::vector<IdAncestor> path;
    path.reserve(Height(m_root));
    auto* node = m_root.get();
    while (node && node->id != id) {
        const bool left = id < node->id;
        path.push_back({node, left});
        node = (left ? node->left : node->right).get();
    }
    if (!node) return;
    IdTree result;
    if (!node->left) result = node->right;
    else if (!node->right) result = node->left;
    else {
        std::vector<IdAncestor> successor_path;
        successor_path.reserve(Height(node->right));
        auto* successor = node->right.get();
        while (successor->left) {
            successor_path.push_back({successor, true});
            successor = successor->left.get();
        }
        result = Balance(Make(successor->id, node->left, Rebuild(successor_path, successor->right)));
    }
    m_root = Rebuild(path, std::move(result));
}

std::optional<Txid> ReceiptIds::next(const std::optional<Txid>& after) const
{
    auto* node = m_root.get();
    const ReceiptIdNode* next{nullptr};
    while (node) {
        if (!after || *after < node->id) {
            next = node;
            node = node->left.get();
        } else node = node->right.get();
    }
    return next ? std::optional<Txid>{next->id} : std::nullopt;
}

Receipt Read(interfaces::Wallet& wallet, const Txid& id)
{
    interfaces::WalletTxStatus status{};
    std::vector<std::string> messages, requests;
    bool mempool{false}; int blocks{0};
    const auto tx = wallet.getWalletTxDetails(id, status, messages, requests, mempool, blocks);
    auto result = Capture(tx, tx, status, mempool);
    result.id = id;
    return result;
}

void Ledger::erase(const Txid& id)
{
    m_unconfirmed.erase(id);
    const auto old = m_payments.find(id);
    if (old == m_payments.end()) return;
    for (const auto& [address, amount] : old->second) {
        auto& total = m_totals.at(address);
        total -= amount;
        if (total == 0) m_totals.erase(address);
    }
    m_payments.erase(old);
}

void Ledger::clearPayments()
{
    m_payments.clear(); m_totals.clear(); m_unconfirmed.clear();
}

std::unique_ptr<Ledger> Ledger::takePayments()
{
    auto old = std::make_unique<Ledger>();
    old->m_payments.swap(m_payments);
    old->m_totals.swap(m_totals);
    std::swap(old->m_unconfirmed, m_unconfirmed);
    return old;
}

void Ledger::apply(const Receipt& receipt, const QSet<QString>& addresses)
{
    std::map<QString, CAmount> payments;
    for (const auto& [address, amount] : receipt.outputs) {
        if (!addresses.contains(address)) continue;
        m_observed.insert(address);
        if (!receipt.active) continue;
        payments.emplace(address, amount);
    }
    const auto old = m_payments.find(receipt.id);
    const bool same_payments = old == m_payments.end() ? payments.empty() : old->second == payments;
    if (!same_payments) {
        if (old != m_payments.end()) {
            for (const auto& [address, amount] : old->second) {
                auto& total = m_totals.at(address);
                total -= amount;
                if (total == 0) m_totals.erase(address);
            }
        }
        for (const auto& [address, amount] : payments) m_totals[address] += amount;
        if (payments.empty()) {
            if (old != m_payments.end()) m_payments.erase(old);
        } else {
            m_payments.insert_or_assign(receipt.id, std::move(payments));
        }
    }
    // Status polling usually changes nothing. In particular, do not erase
    // and reinsert an unchanged unconfirmed node: hardened libraries may
    // validate the entire tree on every erase.
    const bool unconfirmed = receipt.active && receipt.unconfirmed && m_payments.contains(receipt.id);
    if (unconfirmed) {
        if (!m_unconfirmed.contains(receipt.id)) m_unconfirmed.insert(receipt.id);
    } else if (m_unconfirmed.contains(receipt.id)) {
        m_unconfirmed.erase(receipt.id);
    }
}

Reader::Reader(std::shared_ptr<walletopening::Session> opening, Notify notify, Clock clock)
    : m_opening(std::move(opening)), m_notify(std::move(notify)), m_clock(std::move(clock)),
      m_retirement([opening = m_opening](auto release) { return opening->postDomain(std::move(release)); }) {}

void Reader::reload(QSet<QString> addresses, bool rescan)
{
    { std::lock_guard lock{m_mutex}; if (m_closed) return; m_settled = false; ++m_revision; m_addresses = std::move(addresses); m_full = true; m_rescan |= rescan; m_retry_after = {}; }
    schedule();
}

void Reader::changed(std::map<Txid, ChangeType> changes, QSet<QString> addresses)
{
    {
        std::lock_guard lock{m_mutex};
        if (m_closed) return;
        m_settled = false;
        ++m_revision;
        m_addresses = std::move(addresses);
        for (const auto& [id, change] : changes) {
            if (m_changes.size() < 256 || m_changes.contains(id)) m_changes[id] = change;
            else m_full = m_rescan = true;
        }
    }
    schedule();
}

void Reader::poll()
{
    { std::lock_guard lock{m_mutex}; if (m_closed) return; if (m_has_unconfirmed) { m_settled = false; m_poll = true; ++m_revision; } }
    schedule();
}

void Reader::notice(uint64_t revision, std::map<Txid, ChangeType> changes, bool rescan)
{
    {
        std::lock_guard lock{m_mutex};
        if (m_closed || revision <= m_native_revision) return;
        m_settled = false;
        m_native_revision = revision;
        ++m_revision;
        m_full |= rescan; m_rescan |= rescan;
        for (const auto& [id, change] : changes) {
            if (m_changes.size() < 256 || m_changes.contains(id)) m_changes[id] = change;
            else m_full = m_rescan = true;
        }
    }
    schedule();
}

void Reader::retry() { m_retirement.retry(); schedule(); }

void Reader::close()
{
    std::shared_ptr<const RecheckSnapshot> retired;
    {
        std::lock_guard lock{m_mutex};
        m_closed = true;
        m_settled = false;
        m_changes.clear(); m_full = m_poll = false;
        retired = std::exchange(m_recheck, {});
    }
    // Closing a live facade must release the idle root's cleanup lease too.
    // Its value teardown runs on Cleanup, outside this ingress mutex.
}

bool Reader::settled() const
{
    return m_settled.load() && m_opening->available() && m_settled_native_revision.load() == m_opening->transactionRevision();
}

execution::SubmitResult Reader::onSnapshot(Consumer consumer)
{
    const auto self = shared_from_this();
    return m_opening->postDomain([self, consumer = std::move(consumer)]() mutable {
        std::shared_ptr<const Snapshot> replay;
        {
            std::lock_guard lock{self->m_mutex};
            self->m_consumer = std::move(consumer);
            replay = self->m_current;
            consumer = self->m_consumer;
        }
        if (consumer && replay && self->settled()) consumer(std::move(replay));
    });
}

void Reader::retire(std::shared_ptr<const Snapshot> snapshot)
{
    m_retirement.retire(std::move(snapshot));
}

void Reader::schedule()
{
    {
        std::lock_guard lock{m_mutex};
        if (m_closed || m_scheduled || m_active || m_clock() < m_retry_after) return;
        if (!m_full && !m_poll && m_changes.empty() && !m_followup) return;
        m_scheduled = true;
    }
    const auto self = shared_from_this();
    if (!m_opening->postDomain([self] { self->plan(); })) {
        { std::lock_guard lock{m_mutex}; m_scheduled = false; }
        failed(Status::Rejected);
    }
}

bool Reader::restartRechecks()
{
    // Confirmation can remove every current ledger node while this old root
    // still describes required reads. Its final release may be wallet-sized;
    // reserve existing Cleanup ownership before capturing it, so finishing a
    // bounded Domain batch never destroys the full snapshot inline.
    std::shared_ptr<const RecheckSnapshot> next;
    if (!m_ledger.unconfirmed().empty()) {
        const auto slot = std::make_shared<RecheckSlot>(m_recheck_inventory);
        if (!slot->admitted) return false;
        next = walletopening::RetainValues<RecheckSnapshot>(m_opening, [this, slot] {
            return std::make_unique<RecheckSnapshot>(RecheckSnapshot{slot, m_ledger.unconfirmed()});
        });
        if (!next) return false;
    }
    {
        std::lock_guard lock{m_mutex};
        if (m_closed) return false;
        next = std::exchange(m_recheck, std::move(next));
    }
    m_recheck_after.reset();
    m_recheck_skip.clear();
    return true;
}

bool Reader::clearPayments()
{
    if (m_ledger.emptyPayments()) return true;
    const auto slot = std::make_shared<RecheckSlot>(m_recheck_inventory);
    if (!slot->admitted) return false;
    const auto retired = walletopening::RetainValues<RetiredPayments>(m_opening, [this, slot] {
        return std::make_unique<RetiredPayments>(RetiredPayments{slot, m_ledger.takePayments()});
    });
    return bool(retired);
}

std::optional<Txid> Reader::nextRecheck()
{
    // Only the current completed read batch can be skipped (at most 384 IDs).
    std::shared_ptr<const RecheckSnapshot> snapshot;
    {
        std::lock_guard lock{m_mutex};
        if (m_closed) return std::nullopt;
        snapshot = m_recheck;
    }
    if (!snapshot) return std::nullopt;
    auto next = snapshot->ids.next(m_recheck_after);
    while (next && m_recheck_skip.contains(*next)) {
        m_recheck_after = next;
        next = snapshot->ids.next(m_recheck_after);
    }
    if (next) return next;
    {
        std::lock_guard lock{m_mutex};
        // GUI close may already have revoked the original root.
        if (m_recheck == snapshot) m_recheck.reset();
    }
    m_recheck_after.reset();
    m_recheck_skip.clear();
    return std::nullopt;
}

void Reader::plan()
{
    Work work;
    bool poll;
    {
        std::lock_guard lock{m_mutex};
        m_scheduled = false;
        if (m_closed) return;
        work.addresses = m_addresses;
        work.full = std::exchange(m_full, false);
        work.rescan = std::exchange(m_rescan, false);
        work.revision = m_revision;
        work.native_revision = m_native_revision;
        work.changes = std::exchange(m_changes, {});
        poll = std::exchange(m_poll, false);
        m_inflight = work.changes;
        m_active = true;
    }
    if (poll && !restartRechecks()) {
        failed(Status::Rejected);
        return;
    }
    while (work.statuses.size() < 128) {
        const auto id = nextRecheck();
        if (!id) break;
        m_recheck_after = id;
        work.statuses.insert(*id);
    }
    const auto self = shared_from_this();
    // Admission is pending before any read, and stays pending through Domain
    // reconciliation, not merely until the Core function returns.
    publish({{}, {}, Status::Pending, false, false, work.revision});
    if (work.full && !work.addresses.empty()) {
        const auto submitted = m_opening->read([](interfaces::Wallet& wallet) { return wallet.getWalletTxs(); },
            [self, work = std::move(work)](std::set<interfaces::WalletTx> transactions) mutable {
                self->beginScan(std::move(work), std::move(transactions));
            }, [self](std::exception_ptr) { self->failed(Status::Failed); });
        if (!submitted) failed(Status::Rejected);
    } else {
        if (work.full && !clearPayments()) {
            failed(Status::Rejected);
            return;
        }
        readChanges(std::move(work));
    }
}

struct Reader::Scan {
    Work work;
    std::set<interfaces::WalletTx> transactions;
    std::set<interfaces::WalletTx>::const_iterator next;
};

void Reader::beginScan(Work work, std::set<interfaces::WalletTx> transactions)
{
    { std::lock_guard lock{m_mutex}; if (m_closed) return; }
    if (!clearPayments()) {
        failed(Status::Rejected);
        return;
    }
    auto scan = std::make_shared<Scan>();
    scan->work = std::move(work);
    scan->transactions = std::move(transactions);
    scan->next = scan->transactions.begin();
    scanNext(std::move(scan));
}

void Reader::scanNext(std::shared_ptr<Scan> scan)
{
    { std::lock_guard lock{m_mutex}; if (m_closed) return; }
    // getWalletTxs is the public interface's single bulk capture. Subsequent
    // status reads and ledger reductions yield after each bounded batch.
    std::vector<interfaces::WalletTx> batch;
    while (scan->next != scan->transactions.end() && batch.size() < 128) {
        const auto& tx = *scan->next++;
        if (tx.tx) batch.push_back(tx);
    }
    if (batch.empty()) {
        readChanges(std::move(scan->work));
        return;
    }
    const auto self = shared_from_this();
    const auto submitted = m_opening->read([batch = std::move(batch)](interfaces::Wallet& wallet) {
        std::vector<Receipt> receipts;
        receipts.reserve(batch.size());
        for (const auto& tx : batch) {
            interfaces::WalletTxStatus status{};
            std::vector<std::string> messages, requests;
            bool mempool{false}; int blocks{0};
            const auto current = wallet.getWalletTxDetails(tx.tx->GetHash(), status, messages, requests, mempool, blocks);
            receipts.push_back(Capture(tx, current, status, mempool));
        }
        return receipts;
    }, [self, scan = std::move(scan)](std::vector<Receipt> receipts) mutable {
        { std::lock_guard lock{self->m_mutex}; if (self->m_closed) return; }
        for (const auto& receipt : receipts) self->m_ledger.apply(receipt, scan->work.addresses);
        self->scanNext(std::move(scan));
    }, [self](std::exception_ptr) { self->failed(Status::Failed); });
    if (!submitted) failed(Status::Rejected);
}

void Reader::readChanges(Work work)
{
    const auto self = shared_from_this();
    auto changes = work.changes;
    auto addresses = work.addresses;
    const bool rescan = work.rescan;
    const auto revision = work.revision;
    const auto native_revision = work.native_revision;
    // This batch is independently bounded by the ingress (256) and status
    // recheck (128) limits, including when a full scan has just completed.
    const auto submitted = m_opening->read([work = std::move(work)](interfaces::Wallet& wallet) {
        std::vector<Receipt> receipts;
        // A coalesced notification is a reconciliation hint, not a current
        // state receipt. In particular a delayed deletion can precede a later
        // reappearance in the same original wallet opening.
        for (const auto& [id, change] : work.changes) {
            Q_UNUSED(change);
            receipts.push_back(Read(wallet, id));
        }
        for (const auto& id : work.statuses) if (!work.changes.contains(id)) receipts.push_back(Read(wallet, id));
        return receipts;
    }, [self, changes = std::move(changes), addresses = std::move(addresses), rescan, revision, native_revision](std::vector<Receipt> receipts) mutable {
        self->complete(std::move(receipts), std::move(changes), std::move(addresses), rescan, revision, native_revision);
    }, [self](std::exception_ptr) { self->failed(Status::Failed); }, execution::WorkClass::Observation);
    if (!submitted) failed(Status::Rejected);
}

void Reader::complete(std::vector<Receipt> receipts, std::map<Txid, ChangeType> changes,
                      QSet<QString> addresses, bool rescan, uint64_t revision, uint64_t native_revision)
{
    m_retirement.retry();
    {
        std::lock_guard lock{m_mutex};
        if (m_closed) return;
    }
    for (const auto& [id, change] : changes) {
        if (m_announcements.size() < 256 || m_announcements.contains(id)) m_announcements[id] = change;
        else rescan = true;
    }
    // The replacement can pay a different request (or remove an old output),
    // so matching destination addresses cannot identify every stale receipt.
    // Capture prior unconfirmed IDs once per notice batch; status continuations
    // remain bounded, and IDs already read in this batch are removed below.
    if (!changes.empty()) {
        if (!restartRechecks()) {
            failed(Status::Rejected);
            return;
        }
        for (const auto& receipt : receipts) m_recheck_skip.insert(receipt.id);
    }
    for (const auto& receipt : receipts) {
        m_ledger.apply(receipt, addresses);
    }
    const bool rechecking = nextRecheck().has_value();
    // A new payment can replace an older unconfirmed payment without the
    // receiver being notified about that original. Until its status is read,
    // the intermediate ledger may contain both. Keep the last reconciled
    // presentation instead of exposing that temporary double count.
    std::shared_ptr<const Snapshot> snapshot;
    if (!rechecking) snapshot = std::make_shared<Snapshot>(Snapshot{m_opening->identity(), m_ledger.totals(), m_ledger.observed(), native_revision, addresses});
    std::shared_ptr<const Snapshot> previous;
    bool pending;
    {
        std::lock_guard lock{m_mutex};
        m_active = false; m_inflight.clear();
        if (m_closed) return;
        m_failures = 0; m_retry_after = {};
        m_followup = rechecking;
        m_has_unconfirmed = !m_ledger.unconfirmed().empty();
        if (snapshot) previous = std::exchange(m_current, snapshot);
        pending = m_full || m_poll || !m_changes.empty() || m_followup;
    }
    // A replacement's announcement and receipt total describe the same
    // reconciled point, including its unnotified predecessor.
    if (rechecking) changes.clear();
    else changes = std::exchange(m_announcements, {});
    publish({std::move(snapshot), std::move(changes), pending ? Status::Pending : Status::Ready, false, rescan, revision});
    schedule();
}

void Reader::failed(Status status)
{
    {
        std::lock_guard lock{m_mutex};
        m_active = false;
        m_settled = false;
        if (m_closed) return;
        for (const auto& [id, change] : m_inflight) {
            if (m_changes.size() < 256) m_changes.try_emplace(id, change);
        }
        m_inflight.clear();
        m_full = m_rescan = true; // A failed/rejected batch must not silently lose events.
        m_retry_after = m_clock() + std::chrono::seconds{std::min(30U, 1U << m_failures)};
        m_failures = std::min(5U, m_failures + 1);
    }
    publish({{}, {}, status});
}

void Reader::publish(Publication update)
{
    bool notify;
    Consumer consumer;
    std::shared_ptr<const Snapshot> ready;
    {
        std::lock_guard lock{m_mutex};
        if (m_closed) return;
        update.needs_poll = m_has_unconfirmed;
        if (m_latest) {
            update.rescan |= m_latest->rescan;
            if (!update.snapshot) update.snapshot = m_latest->snapshot;
            for (const auto& [id, change] : m_latest->transactions) {
                if (update.transactions.size() < 256) update.transactions.try_emplace(id, change);
                else m_full = m_rescan = true;
            }
        }
        if (update.status == Status::Ready && (update.revision != m_revision ||
            (update.snapshot && update.snapshot->native_revision != m_opening->transactionRevision()) ||
            m_full || m_poll || m_scheduled || m_active || m_followup || !m_changes.empty())) update.status = Status::Pending;
        if (update.status == Status::Ready && update.snapshot) {
            m_settled_native_revision = update.snapshot->native_revision;
            m_settled = true;
            consumer = m_consumer; ready = update.snapshot;
        } else m_settled = false;
        m_latest = std::move(update);
        notify = !std::exchange(m_notice_pending, true);
    }
    // An open request follows this notice. The consumer updates the request
    // history, which must not reach the GUI first.
    if (notify) m_notify();
    if (consumer && settled()) consumer(std::move(ready));
}

std::optional<Publication> Reader::take()
{
    std::lock_guard lock{m_mutex};
    m_notice_pending = false;
    // A new intent can arrive after the old Ready was queued but before GUI
    // takes it. Its old snapshot may show progress, never current readiness.
    if (m_latest && m_latest->status == Status::Ready &&
        (m_latest->revision != m_revision ||
         (m_latest->snapshot && m_latest->snapshot->native_revision != m_opening->transactionRevision()) ||
         m_full || m_poll || m_scheduled || m_active || m_followup || !m_changes.empty())) m_latest->status = Status::Pending;
    return std::exchange(m_latest, {});
}
} // namespace walletreceipts

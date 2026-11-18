// Copyright (c) 2025-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/models/coinslistmodel.h>
#include <qml/guipresentation.h>
#include <qml/bitcoinamount.h>
#include <qml/models/walletqmlmodel.h>
#include <qml/retainedvalue.h>
#include <key_io.h>
#include <script/solver.h>
#include <QLocale>
#include <QPointer>
#include <algorithm>
#include <array>
#include <mutex>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <utility>

struct CoinsListModel::Coin {
    COutPoint outpoint;
    QString address, note, id, amount_text, group;
    CAmount amount{0};
    QDateTime date;
    bool locked{false}, selected{false}, first{false}, last{false}, marked{false};
    bool operator==(const Coin&) const = default;
};
struct CoinsListModel::Controls {
    QString search, sort, filter, group;
    qint64 min{-1}, max{-1};
    bool descending{true};
    quint64 revision{0};
};
struct CoinsListModel::View {
    std::vector<Coin> rows;
    std::map<QString, size_t> by_id;
    std::map<QString, COutPoint> all_ids;
    std::shared_ptr<const Selection> selection, marks;
    Controls controls;
    quint64 selection_revision{0}, lock_revision{0}, mark_revision{0};
    coinselection::InputMode input_mode{coinselection::InputMode::Automatic};
    int count{0}, locked_count{0}, missing_selected_coins{0};
    qint64 total{0}, locked_total{0}, selected_total{0}, min{0}, max{0};
    QString selected_text;
    bool read_failed{false};
};

// Order-statistic AVL tree. Single-row edits never shift a wallet-sized GUI
// vector. Node identity survives rotations/moves; Qt owns persistent indexes.
struct CoinsListModel::Rows {
    struct Node {
        std::shared_ptr<const Coin> value;
        std::unique_ptr<Node> left, right;
        Node* parent{nullptr};
        int height{1}, count{1};
    };
    std::unique_ptr<Node> root;
    std::map<QString, Node*> by_id;
    static int size(const std::unique_ptr<Node>& n) { return n ? n->count : 0; }
    static int height(const std::unique_ptr<Node>& n) { return n ? n->height : 0; }
    static void update(Node* n) {
        n->height = 1 + std::max(height(n->left), height(n->right));
        n->count = 1 + size(n->left) + size(n->right);
        if (n->left) n->left->parent = n;
        if (n->right) n->right->parent = n;
    }
    static void rotateLeft(std::unique_ptr<Node>& node) {
        auto parent = node->parent;
        auto pivot = std::move(node->right);
        node->right = std::move(pivot->left);
        update(node.get());
        pivot->left = std::move(node);
        update(pivot.get());
        pivot->parent = parent;
        node = std::move(pivot);
    }
    static void rotateRight(std::unique_ptr<Node>& node) {
        auto parent = node->parent;
        auto pivot = std::move(node->left);
        node->left = std::move(pivot->right);
        update(node.get());
        pivot->right = std::move(node);
        update(pivot.get());
        pivot->parent = parent;
        node = std::move(pivot);
    }
    static void balance(std::unique_ptr<Node>& n) {
        if (!n) return;
        update(n.get());
        if (height(n->left) - height(n->right) > 1) {
            if (height(n->left->right) > height(n->left->left)) rotateLeft(n->left);
            rotateRight(n);
        } else if (height(n->right) - height(n->left) > 1) {
            if (height(n->right->left) > height(n->right->right)) rotateRight(n->right);
            rotateLeft(n);
        }
    }
    // A Qt row count fits int; an AVL tree of that size has height below64.
    // Explicit ancestor paths avoid recursive GUI stack growth during edits.
    static void insert(std::unique_ptr<Node>& tree, int row, std::unique_ptr<Node> node) {
        std::array<std::unique_ptr<Node>*,64> path{}; size_t depth{0};
        auto* link=&tree;
        while(*link){
            Q_ASSERT(depth<path.size());path[depth++]=link;
            const int before=size((*link)->left);
            if(row<=before)link=&(*link)->left;
            else{row-=before+1;link=&(*link)->right;}
        }
        *link=std::move(node);
        while(depth)balance(*path[--depth]);
    }
    static std::unique_ptr<Node> take(std::unique_ptr<Node>& tree, int row) {
        std::array<std::unique_ptr<Node>*,64> path{};size_t depth{0};
        auto* link=&tree;
        while(row!=size((*link)->left)){
            Q_ASSERT(depth<path.size());path[depth++]=link;
            const int before=size((*link)->left);
            if(row<before)link=&(*link)->left;
            else{row-=before+1;link=&(*link)->right;}
        }
        auto removed=std::move(*link);auto* parent=removed->parent;
        if(!removed->right)*link=std::move(removed->left);
        else{
            std::array<std::unique_ptr<Node>*,64> successors{};size_t count{0};
            auto* successor_link=&removed->right;
            while((*successor_link)->left){
                Q_ASSERT(count<successors.size());successors[count++]=successor_link;
                successor_link=&(*successor_link)->left;
            }
            auto successor=std::move(*successor_link);
            *successor_link=std::move(successor->right);
            while(count)balance(*successors[--count]);
            successor->left=std::move(removed->left);successor->right=std::move(removed->right);
            *link=std::move(successor);
        }
        if(*link)(*link)->parent=parent;
        balance(*link);
        removed->left.reset();
        removed->right.reset();
        removed->parent = nullptr;
        removed->height = removed->count = 1;
        while(depth)balance(*path[--depth]);
        return removed;
    }
    int count() const { return size(root); }
    Node* at(int row) const {
        auto* node = root.get();
        while (node) {
            const int before = size(node->left);
            if (row == before) return node;
            if (row < before) node = node->left.get();
            else { row -= before + 1; node = node->right.get(); }
        }
        return nullptr;
    }
    int rank(Node* node) const {
        int row = size(node->left);
        while (node->parent) {
            if (node == node->parent->right.get()) row += size(node->parent->left) + 1;
            node = node->parent;
        }
        return row;
    }
    void add(int row, std::shared_ptr<const Coin> value) {
        auto node = std::make_unique<Node>(); node->value = std::move(value);
        by_id.emplace(node->value->id, node.get()); insert(root, row, std::move(node)); root->parent = nullptr;
    }
    void erase(int row) { auto node = take(root, row); by_id.erase(node->value->id); if (root) root->parent = nullptr; }
    void move(int from, int to) { auto node = take(root, from); insert(root, to, std::move(node)); root->parent = nullptr; }
};

struct CoinsListModel::Backend : std::enable_shared_from_this<Backend> {
    struct Native {
        std::vector<Coin> coins;
        std::map<QString, size_t> by_id;
        Selection outpoints;
        quint64 lock_revision{0}, selection_revision{0};
    };
    struct Input { std::mutex mutex; Controls controls; };
    struct Chunk { std::vector<LockItemOutcome> items; Selection changed; bool failed{false}, uncertain{false}, already_locked{false}; };
    struct LockJob {
        std::shared_ptr<const walletopening::Session::Action> action;
        std::vector<COutPoint> targets;
        std::vector<LockItemOutcome> items;
        std::shared_ptr<const Selection> marks_at_admission;
        Selection changed, selected_after;
        size_t cursor{0}, requested{0};
        bool locked{false}, failed{false}, uncertain{false}, already_locked{false};
        std::function<void(LockResult)> done;
    };
    const std::shared_ptr<walletopening::Session> opening;
    const execution::SnapshotServices services;
    std::shared_ptr<Input> input{std::make_shared<Input>()};
    std::shared_ptr<execution::SnapshotQuery<std::shared_ptr<const Native>>> native_query;
    std::shared_ptr<execution::SnapshotQuery<std::shared_ptr<const View>>> projection;
    std::shared_ptr<const Native> native;
    std::unique_ptr<coinselection::Session> session;
    std::shared_ptr<std::atomic<quint64>> selection_epoch{std::make_shared<std::atomic<quint64>>(0)};
    std::shared_ptr<std::atomic<quint64>> lock_revision{std::make_shared<std::atomic<quint64>>(0)};
    std::deque<std::shared_ptr<LockJob>> locks;
    bool read_failed{false};
    Backend(std::shared_ptr<walletopening::Session> owner, execution::SnapshotServices ports)
        : opening(std::move(owner)), services(std::move(ports)) {}
    void start(std::function<void()> notify) {
        session = std::make_unique<coinselection::Session>([opening = opening](Selection values) {
            return walletopening::RetainValues<Selection>(opening, [values = std::move(values)]() mutable {
                return std::make_unique<Selection>(std::move(values));
            });
        });
        if (!session->ready()) throw std::runtime_error{"Coin selection cleanup could not be reserved"};
        const auto weak = weak_from_this();
        native_query = std::make_shared<execution::SnapshotQuery<std::shared_ptr<const Native>>>(services.scheduler,
            [opening = opening, revision = lock_revision, selected_revision = selection_epoch] {
                const auto observed = revision->load(), observed_selection = selected_revision->load();
                auto wallet = opening->borrow();
                if (!wallet) throw execution::NotStarted{execution::NotStarted::Cause::Cancelled};
                auto value = walletopening::RetainValues<Native>(opening, [&] {
                    auto result = std::make_unique<Native>(); result->lock_revision = observed; result->selection_revision = observed_selection;
                    for (const auto& [ancestor, outputs] : wallet->listCoins()) {
                        for (const auto& [outpoint, output] : outputs) {
                            if (output.is_spent) continue;
                            CTxDestination destination;
                            if (!ExtractDestination(output.txout.scriptPubKey, destination)) destination = ancestor;
                            std::string label; wallet->getAddress(destination, &label, nullptr);
                            Coin coin; coin.outpoint = outpoint;
                            coin.id = QString::fromStdString(outpoint.hash.GetHex()) + ":" + QString::number(outpoint.n);
                            coin.address = QString::fromStdString(EncodeDestination(destination)); coin.note = QString::fromStdString(label);
                            coin.amount = output.txout.nValue; coin.amount_text = BitcoinAmount::satsToBtcString(coin.amount);
                            coin.date = QDateTime::fromSecsSinceEpoch(output.time); coin.locked = wallet->isLockedCoin(outpoint);
                            result->by_id.emplace(coin.id, result->coins.size()); result->outpoints.insert(outpoint);
                            result->coins.push_back(std::move(coin));
                        }
                    }
                    return result;
                });
                if (!value) throw std::runtime_error{"Coin snapshot cleanup could not be reserved"};
                return std::shared_ptr<const Native>{std::move(value)};
            }, [weak] { if (const auto self = weak.lock()) self->consumeNative(); });
        projection = std::make_shared<execution::SnapshotQuery<std::shared_ptr<const View>>>(services.scheduler,
            [weak] { if (const auto self = weak.lock()) return self->build(); return std::shared_ptr<const View>{}; },
            std::move(notify), execution::Lane::Domain);
    }
    void consumeNative() {
        const auto result = native_query->take(); if (!result) return;
        read_failed = bool(result->error);
        if (result->snapshot && *result->snapshot && locks.empty()) {
            const auto value = *result->snapshot;
            if (value->lock_revision != lock_revision->load()) { native_query->request(); return; }
            native = value;
            // A missing selected input is an explicit reconciliation state.
            // Keep the user's Manual intent until they choose a replacement
            // or explicitly switch back to Automatic.
            Selection spendable;
            for (const auto& coin : native->coins) if (!coin.locked) spendable.insert(coin.outpoint);
            session->reconcileCatalog(spendable);
            session->retainMarks(native->outpoints);
            // Spendability belongs to this native inventory, independently of
            // a newer selection intent. Never expose new rows while Apply
            // would still reconcile them against an older catalog.
            if (native->selection_revision != session->revision()) { native_query->request(); projection->request(); return; }
        }
        projection->request();
    }
    std::shared_ptr<const View> build() {
        if (!native) { if (read_failed) throw std::runtime_error{"Wallet coins could not be read"}; return {}; }
        if (native->coins.size() > static_cast<size_t>(std::numeric_limits<int>::max())) throw std::runtime_error{"Coin inventory exceeds Qt model index range"};
        Controls controls; { std::lock_guard lock{input->mutex}; controls = input->controls; }
        auto result = walletopening::RetainValues<View>(opening, [&] {
            const auto selection = session->selection();
            const auto marks = session->marks();
            auto view = std::make_unique<View>(); view->controls = controls; view->selection = selection; view->marks = marks; view->mark_revision = session->marksRevision();
            view->input_mode = session->inputMode();
            view->missing_selected_coins = session->missing().size();
            view->selection_revision = session->revision(); view->lock_revision = native->lock_revision; view->read_failed = read_failed;
            view->count = native->coins.size();
            bool first{true};
            for (const auto& coin : native->coins) {
                view->all_ids.emplace(coin.id, coin.outpoint);
                view->total += coin.amount;
                if (coin.locked) { ++view->locked_count; view->locked_total += coin.amount; }
                if (selection->contains(coin.outpoint)) view->selected_total += coin.amount;
                if (first) { view->min = view->max = coin.amount; first = false; }
                else { view->min = std::min(view->min, qint64{coin.amount}); view->max = std::max(view->max, qint64{coin.amount}); }
                if (!controls.search.isEmpty() && !coin.note.contains(controls.search, Qt::CaseInsensitive) &&
                    !coin.address.contains(controls.search, Qt::CaseInsensitive) && !coin.id.contains(controls.search, Qt::CaseInsensitive)) continue;
                if ((controls.filter == "locked" && !coin.locked) || (controls.filter == "spendable" && coin.locked)) continue;
                if ((controls.min >= 0 && coin.amount < controls.min) || (controls.max >= 0 && coin.amount > controls.max)) continue;
                auto row = coin; row.selected = selection->contains(coin.outpoint); row.marked = marks->contains(coin.outpoint);
                row.group = controls.group == "address" ? coin.address : QLocale().toString(coin.date, controls.group == "month" ? "MMMM yyyy" : "MMM d, yyyy");
                view->rows.push_back(std::move(row));
            }
            std::sort(view->rows.begin(), view->rows.end(), [&](const Coin& x, const Coin& y) {
                if (controls.group == "address" && x.address != y.address) return x.address < y.address;
                if (controls.group == "month" || controls.group == "date") {
                    const auto format = controls.group == "month" ? "yyyyMM" : "yyyyMMdd";
                    const auto xm = x.date.toString(format), ym = y.date.toString(format);
                    if (xm != ym) return controls.descending ? xm > ym : xm < ym;
                }
                int cmp{0};
                if (controls.sort == "amount") cmp = x.amount < y.amount ? -1 : x.amount > y.amount ? 1 : 0;
                else if (controls.sort == "label") cmp = QString::localeAwareCompare(x.note.toCaseFolded(), y.note.toCaseFolded());
                else cmp = x.date < y.date ? -1 : x.date > y.date ? 1 : 0;
                return cmp == 0 ? x.outpoint < y.outpoint : controls.descending ? cmp > 0 : cmp < 0;
            });
            for (size_t row{0}; row < view->rows.size(); ++row) {
                auto& value = view->rows[row];
                value.first = row == 0 || view->rows[row - 1].group != value.group;
                value.last = row + 1 == view->rows.size() || view->rows[row + 1].group != value.group;
                view->by_id.emplace(value.id, row);
            }
            view->selected_text = BitcoinAmount::satsToBtcString(view->selected_total);
            return view;
        });
        if (!result) throw std::runtime_error{"Coin view cleanup could not be reserved"};
        return result;
    }
    SelectionResult select(std::optional<COutPoint> outpoint, SelectionMode mode, const std::shared_ptr<const Selection>& replacement) {
        auto intent = coinselection::Session::Intent::Toggle;
        switch (mode) {
        case SelectionMode::Unselect: intent = coinselection::Session::Intent::Unselect; break;
        case SelectionMode::Select: intent = coinselection::Session::Intent::Select; break;
        case SelectionMode::Toggle: intent = coinselection::Session::Intent::Toggle; break;
        case SelectionMode::Replace: intent = coinselection::Session::Intent::Replace; break;
        case SelectionMode::Begin: intent = coinselection::Session::Intent::Begin; break;
        case SelectionMode::Cancel: intent = coinselection::Session::Intent::Cancel; break;
        case SelectionMode::Apply: intent = coinselection::Session::Intent::Apply; break;
        case SelectionMode::AutomaticInputs: intent = coinselection::Session::Intent::ChooseAutomatic; break;
        case SelectionMode::ManualInputs: intent = coinselection::Session::Intent::ChooseManual; break;
        }
        const auto result = session->apply(intent, outpoint, replacement);
        if (result.changed) {
            selection_epoch->store(result.revision);
            try { projection->request(); } catch (...) { read_failed = true; }
        }
        return {result.selection, result.revision, result.failed, result.mode, session->editing()};
    }
    MarkOutcome mark(std::optional<COutPoint> outpoint) {
        const auto change = session->toggleMark(outpoint);
        if (change.failed) return {change.marks, change.revision, true};
        try { projection->request(); } catch (...) { read_failed = true; }
        return {change.marks, change.revision, false};
    }
    void enqueueLock(std::shared_ptr<const walletopening::Session::Action> action, const QStringList& ids, bool locked, std::function<void(LockResult)> done, bool marked = false) {
        auto job = walletopening::RetainValues<LockJob>(opening, [&] {
            auto next = std::make_unique<LockJob>(); next->action = action; next->locked = locked; next->done = done;
            const auto marks = session->marks();
            if (marked) {
                next->requested = marks ? marks->size() : 0;
                next->marks_at_admission = marks;
                if (!marks || marks->empty()) { next->failed = true; return next; }
                for (const auto& target : *marks) {
                    const QString id = QString::fromStdString(target.hash.GetHex()) + ":" + QString::number(target.n);
                    next->items.push_back({id, target, LockItemOutcome::State::NotAttempted});
                    if (!native || !native->outpoints.contains(target)) {
                        next->items.back().state = LockItemOutcome::State::Failed;
                        next->failed = true;
                        continue;
                    }
                    next->targets.push_back(target);
                }
                if (next->failed) next->targets.clear();
                return next;
            }
            const std::set<QString> requested{ids.begin(), ids.end()};
            next->requested = requested.size();
            if (requested.empty()) { next->failed = true; return next; }
            if (!native) {
                for (const auto& id : requested) next->items.push_back({id, {}, LockItemOutcome::State::Failed});
                next->failed = true;
                return next;
            }
            for (const auto& id : requested) {
                const auto found = native->by_id.find(id);
                if (found == native->by_id.end()) {
                    next->items.push_back({id, {}, LockItemOutcome::State::Failed});
                    next->failed = true;
                    continue;
                }
                const auto target = native->coins[found->second].outpoint;
                next->items.push_back({id, target, LockItemOutcome::State::NotAttempted});
                next->targets.push_back(target);
            }
            if (next->failed) next->targets.clear();
            return next;
        });
        if (!job) {
            done({true, false, static_cast<size_t>(ids.size()), 0, session->selection(), session->revision(), session->marks(), session->marksRevision(), {}});
            return;
        }
        locks.push_back(std::move(job)); ++*lock_revision;
        if (marked) { session->toggleMark(); try { projection->request(); } catch (...) { read_failed = true; } }
        if (locks.size() == 1) lockChunk();
    }
    void lockChunk() {
        while(!locks.empty()) {
        const auto self = shared_from_this(); const auto job = locks.front();
        if (job->failed || job->cursor == job->targets.size()) { finishLock(); continue; }
        const auto begin = job->cursor, end = std::min(begin + 32, job->targets.size());
        const auto admitted = opening->perform(job->action, [job, begin, end](interfaces::Wallet& wallet) {
            Chunk result;
            result.items.reserve(end - begin);
            for (auto i = begin; i < end; ++i) {
                auto item = job->items[i];
                try {
                    const auto& target = job->targets[i];
                    // A displayed row is not an action precondition. Query the
                    // original public wallet immediately before locking; the
                    // captured output may have been spent since enumeration.
                    // Unlock must still be able to clear an obsolete lock.
                    if (job->locked) {
                        bool available{false}, already_locked{false};
                        try {
                            const auto current = wallet.getCoins({target});
                            available = current.size() == 1 && !current.front().txout.IsNull()
                                && !current.front().is_spent && current.front().depth_in_main_chain >= 0;
                            if (available) already_locked = wallet.isLockedCoin(target);
                        } catch (...) { available = false; /* No mutation was attempted. */ }
                        if (!available || already_locked) {
                            // Public Wallet cannot report lock persistence.
                            // Never silently upgrade a possibly temporary lock.
                            item.state = already_locked ? LockItemOutcome::State::AlreadyLocked : LockItemOutcome::State::Failed;
                            result.already_locked |= already_locked;
                            result.failed = true;
                            result.items.push_back(std::move(item));
                            continue;
                        }
                    }
                    if (job->locked ? wallet.lockCoin(target, true) : wallet.unlockCoin(target)) {
                        item.state = LockItemOutcome::State::Changed;
                        result.changed.insert(target);
                    } else {
                        item.state = LockItemOutcome::State::Failed;
                        result.failed = true;
                    }
                } catch (...) {
                    item.state = LockItemOutcome::State::Unknown;
                    result.uncertain = true;
                }
                result.items.push_back(std::move(item));
                if (result.uncertain) break;
            }
            return result;
        }, [self, job, begin](Chunk result) {
            job->cursor = begin + result.items.size(); job->changed.insert(result.changed.begin(), result.changed.end());
            for (size_t i = 0; i < result.items.size(); ++i) job->items[begin + i] = std::move(result.items[i]);
            job->failed |= result.failed; job->uncertain |= result.uncertain; job->already_locked |= result.already_locked;
            if (result.uncertain) self->finishLock();
            self->lockChunk();
        }, [self, job, begin, end](std::exception_ptr) {
            for (auto i = begin; i < end; ++i) job->items[i].state = LockItemOutcome::State::Unknown;
            job->cursor = end; job->uncertain = true; self->finishLock(); self->lockChunk();
        });
        if(admitted)return;
        job->failed = true; finishLock();
        }
    }
    void finishLock() {
        const auto job = locks.front();
        // A moved-from std::function may retain its inline callable (libc++).
        // Clear the job before publishing an outcome that aliases its values,
        // otherwise its completion can keep that same cleanup lease alive.
        auto done = std::exchange(job->done, {});
        Selection restore;
        if (job->marks_at_admission) for (const auto& item : job->items) {
            if (item.state != LockItemOutcome::State::Changed && !item.coin_id.isEmpty()) restore.insert(item.outpoint);
        }
        const auto prior_selection_revision = session->revision();
        Selection locked_successes;
        if (job->locked) locked_successes = job->changed;
        if (!session->applyLockResults(locked_successes, restore)) job->uncertain = true;
        if (session->revision() != prior_selection_revision) selection_epoch->store(session->revision());
        ++*lock_revision;
        const auto requested = job->requested, changed = job->changed.size();
        auto items = std::move(job->items);
        job->action.reset();
        std::vector<COutPoint>{}.swap(job->targets); Selection{}.swap(job->changed);
        done({job->failed, job->uncertain, requested, changed, session->selection(), session->revision(), session->marks(), session->marksRevision(), std::move(items), job->already_locked});
        locks.pop_front();
        if (locks.empty()) native_query->request();
    }
};

struct CoinsListModel::Retired {
    std::shared_ptr<Backend> backend;
    std::shared_ptr<Rows> rows;
    std::shared_ptr<const View> target, published;
    std::shared_ptr<const Selection> selected;
    std::deque<LockCommand> locks;
    std::deque<std::shared_ptr<execution::OwnedCommand<MarkOutcome>>> marks;
    std::deque<std::shared_ptr<execution::OwnedCommand<SelectionResult>>> selections;
    std::shared_ptr<execution::OwnedCommand<LockResult>> last_lock;
    std::shared_ptr<execution::OwnedCommand<SelectionResult>> last_selection;
};

CoinsListModel::CoinsListModel(WalletQmlModel* parent) : QAbstractListModel(parent), m_wallet_model(parent)
{
    if (parent) {
        connect(parent, &WalletQmlModel::transactionChanged, this, &CoinsListModel::update);
        connect(parent, &WalletQmlModel::addressListChanged, this, &CoinsListModel::update);
        connect(parent, &WalletQmlModel::walletContentsChanged, this, &CoinsListModel::update);
    }
}
CoinsListModel::~CoinsListModel() { close(); retireValues(); }
void CoinsListModel::setOpening(std::shared_ptr<walletopening::Session> opening, execution::SnapshotServices services)
{
    if (m_stopping || !opening || (m_opening && m_opening != opening) || m_backend) return;
    m_opening = std::move(opening); m_services = std::move(services);
    const auto setup = ++m_setup_revision;
    if (!m_services.scheduler) m_services.scheduler = m_opening->scheduler();
    const QPointer<CoinsListModel> observer{this};
    if (!m_services.post) m_services.post = presentation::QueuedPost();
    const auto original_opening = m_opening;
    const auto ports = m_services;
    const auto current = [observer, setup, original_opening, ports] {
        return observer && !observer->m_stopping && observer->m_setup_revision == setup &&
            observer->m_opening == original_opening && observer->m_services.scheduler == ports.scheduler;
    };
    try {
        auto retired = walletopening::RetainValues<Retired>(original_opening, [] { return std::make_unique<Retired>(); });
        if (!current()) return;
        if (!retired) throw std::runtime_error{"Coin retirement cleanup could not be reserved"};
        auto rows = walletopening::RetainValues<Rows>(original_opening, [] { return std::make_unique<Rows>(); });
        if (!current()) return;
        if (!rows) throw std::runtime_error{"Coin presentation cleanup could not be reserved"};
        auto backend = std::make_shared<Backend>(original_opening, ports);
        backend->start([post = ports.post, observer, setup] {
            post([observer, setup] { if (observer && observer->m_setup_revision == setup) observer->publishView(); });
        });
        if (!current()) return;
        m_retired = std::move(retired); m_rows = std::move(rows); m_backend = std::move(backend);
        m_selection = m_backend->session->selection();
        rebuildView(); if (!current()) return;
        update();
    } catch (...) {
        if (!current()) return;
        m_backend.reset(); m_rows.reset();
        m_read_error = tr("Wallet coin services could not be initialized."); Q_EMIT readErrorChanged();
    }
}
void CoinsListModel::close()
{
    if (m_stopping) return;
    m_stopping = true; ++m_reconcile_turn;
    if (m_backend) {
        if (m_backend->native_query) m_backend->native_query->close();
        if (m_backend->projection) m_backend->projection->close();
    }
    if (!m_changing_rows) retireValues();
}
void CoinsListModel::retireValues()
{
    if (auto retired = std::move(m_retired)) {
        retired->backend = std::move(m_backend); retired->rows = std::move(m_rows);
        retired->target = std::move(m_target); retired->published = std::move(m_published_view);
        retired->selected = std::move(m_selection);
        retired->locks = std::move(m_lock_commands); retired->marks = std::move(m_mark_operations);
        retired->selections = std::move(m_selection_operations);
        retired->last_lock = std::move(m_last_lock); retired->last_selection = std::move(m_last_selection);
        // Final container and observation destruction runs on reserved Native
        // cleanup. Accepted actions retain their original backend independently.
    }
}
int CoinsListModel::rowCount(const QModelIndex& parent) const { return parent.isValid() || !m_rows ? 0 : m_rows->count(); }
QVariant CoinsListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.model() != this || index.column() != 0 || index.row() < 0 || index.row() >= rowCount()) return {};
    const auto& coin = *m_rows->at(index.row())->value;
    switch (role) {
    case AddressRole: return coin.address;
    case AmountRole: return coin.amount_text;
    case AmountSatoshiRole: return static_cast<qint64>(coin.amount);
    case DateTimeRole: return coin.date;
    case LabelRole: return coin.note;
    case LockedRole: return coin.locked;
    case SelectedRole: return coin.selected;
    case CoinIdRole: return coin.id;
    case GroupRole: return coin.group;
    case GroupFirstRole: return coin.first;
    case GroupLastRole: return coin.last;
    case MarkedRole: return coin.marked;
    default: return {};
    }
}
QHash<int, QByteArray> CoinsListModel::roleNames() const
{
    return {{AddressRole,"address"}, {AmountRole,"amount"}, {DateTimeRole,"date"}, {LabelRole,"label"},
        {LockedRole,"locked"}, {SelectedRole,"selected"}, {AmountSatoshiRole,"amountSatoshi"}, {CoinIdRole,"coinId"},
        {GroupRole,"groupTitle"}, {GroupFirstRole,"groupFirst"}, {GroupLastRole,"groupLast"}, {MarkedRole,"walletMarked"}};
}
void CoinsListModel::update()
{
    if (m_stopping || !m_backend || !m_opening->available()) return;
    const QPointer<CoinsListModel> guard{this};
    const auto query = m_backend->native_query;
    const auto setup = m_setup_revision;
    m_loading = true; query->request();
    if (guard && !m_stopping && m_setup_revision == setup) Q_EMIT viewChanged();
}
void CoinsListModel::rebuildView()
{
    if (m_stopping || !m_backend) return;
    ++m_reconcile_turn; m_target.reset();
    Controls controls{m_search, m_sort, m_filter, m_group, m_min_amount, m_max_amount, m_descending, ++m_view_revision};
    { std::lock_guard lock{m_backend->input->mutex}; m_backend->input->controls = std::move(controls); }
    const QPointer<CoinsListModel> guard{this};
    const auto query = m_backend->projection;
    const auto setup = m_setup_revision, revision = m_view_revision;
    m_loading = true; query->request();
    if (guard && !m_stopping && m_setup_revision == setup && m_view_revision == revision) Q_EMIT viewChanged();
}
void CoinsListModel::publishView()
{
    if (m_stopping || !m_backend) return;
    const auto result = m_backend->projection->take(); if (!result) return;
    const QPointer<CoinsListModel> guard{this};
    const auto value = result->snapshot ? *result->snapshot : std::shared_ptr<const View>{};
    const auto error = result->error || (value && value->read_failed) ? tr("Wallet coins could not be refreshed.") : QString{};
    if (error != m_read_error) { m_read_error = error; Q_EMIT readErrorChanged(); if (!guard || m_stopping) return; }
    if (!value) { m_loading = false; Q_EMIT viewChanged(); return; }
    if (value->controls.revision != m_view_revision || value->selection_revision < m_selection_revision ||
        value->lock_revision != m_backend->lock_revision->load() || value->mark_revision < m_mark_revision) return;
    m_target = value; m_incomplete = true; m_remove_cursor = rowCount() - 1; m_insert_cursor = 0;
    const auto turn = ++m_reconcile_turn;
    Q_EMIT viewChanged(); if (!guard || m_stopping || turn != m_reconcile_turn) return;
    reconcile(turn);
}
void CoinsListModel::reconcile(quint64 turn)
{
    if (m_stopping || m_changing_rows || turn != m_reconcile_turn || !m_target) return;
    const QPointer<CoinsListModel> guard{this};
    const auto target = m_target; const auto rows = m_rows;
    if (target->selection_revision < m_selection_revision || target->controls.revision != m_view_revision ||
        target->lock_revision != m_backend->lock_revision->load() || target->mark_revision < m_mark_revision) {
        m_target.reset(); ++m_reconcile_turn; m_loading = true;
        m_backend->projection->request(); return;
    }
    int budget{32};
    while (budget > 0 && m_remove_cursor >= 0) {
        const int row = m_remove_cursor--; --budget;
        if (!target->by_id.contains(rows->at(row)->value->id)) {
            m_changing_rows = true; beginRemoveRows({}, row, row); if (!guard) return;
            rows->erase(row); endRemoveRows(); if (!guard) return;
            m_changing_rows = false; if (m_stopping) { retireValues(); return; }
            if (turn != m_reconcile_turn) return;
        }
    }
    while (budget > 0 && m_remove_cursor < 0 && m_insert_cursor < static_cast<int>(target->rows.size())) {
        const int row = m_insert_cursor++; --budget;
        const auto value = std::shared_ptr<const Coin>{target, &target->rows[row]};
        const auto found = rows->by_id.find(value->id);
        if (found == rows->by_id.end()) {
            m_changing_rows = true; beginInsertRows({}, row, row); if (!guard) return;
            rows->add(row, value); endInsertRows(); if (!guard) return;
            m_changing_rows = false; if (m_stopping) { retireValues(); return; }
            if (turn != m_reconcile_turn) return;
        } else {
            const int from = rows->rank(found->second);
            if (from != row) {
                m_changing_rows = true; beginMoveRows({}, from, from, {}, from < row ? row + 1 : row); if (!guard) return;
                rows->move(from, row); endMoveRows(); if (!guard) return;
                m_changing_rows = false; if (m_stopping) { retireValues(); return; }
                if (turn != m_reconcile_turn) return;
            }
            auto* node = rows->at(row);
            if (*node->value != *value) {
                node->value = value; Q_EMIT dataChanged(index(row), index(row));
                if (!guard || m_stopping || turn != m_reconcile_turn) return;
            } else node->value = value;
        }
    }
    if (m_remove_cursor >= 0 || m_insert_cursor < static_cast<int>(target->rows.size())) {
        try { m_services.post([guard, turn] { if (guard) guard->reconcile(turn); }); }
        catch (...) { m_read_error = tr("Coin presentation is waiting for GUI delivery."); Q_EMIT readErrorChanged(); }
        return;
    }
    if (target->selection_revision < m_selection_revision || target->lock_revision != m_backend->lock_revision->load() || target->mark_revision < m_mark_revision) {
        m_target.reset(); ++m_reconcile_turn; m_backend->projection->request(); return;
    }
    const bool marks_changed = m_mark_revision != target->mark_revision || m_marked_count != static_cast<int>(target->marks->size());
    m_mark_revision = target->mark_revision; m_marked_count = target->marks->size();
    const bool selection_changed = m_selection_revision != target->selection_revision || m_selection != target->selection ||
        m_input_mode != target->input_mode || m_missing_selected_coins != target->missing_selected_coins;
    m_coin_count = target->count; m_locked_count = target->locked_count;
    m_total = target->total; m_locked_total = target->locked_total; m_selected_total = target->selected_total;
    m_available_min = target->min; m_available_max = target->max; m_selected_text = target->selected_text;
    m_selection = target->selection; m_selection_revision = target->selection_revision;
    m_input_mode = target->input_mode; m_missing_selected_coins = target->missing_selected_coins;
    m_selected_count = m_selection->size(); m_published_view = target; m_target.reset(); m_loading = false; m_incomplete = false;
    if (marks_changed) { Q_EMIT marksChanged(); if (!guard || m_stopping) return; }
    Q_EMIT coinCountChanged(); if (!guard || m_stopping) return;
    Q_EMIT lockedCoinsCountChanged(); if (!guard || m_stopping) return;
    Q_EMIT selectedCoinsCountChanged(); if (!guard || m_stopping) return;
    if (selection_changed) { Q_EMIT selectionSnapshotChanged(); if (!guard || m_stopping) return; }
    Q_EMIT viewChanged();
}
void CoinsListModel::retryPublications()
{
    if (m_stopping) return;
    const QPointer<CoinsListModel> guard{this};
    publishLocks(); if (!guard || m_stopping) return;
    publishSelections(); if (!guard || m_stopping) return;
    publishMarks(); if (!guard || m_stopping) return;
    publishView(); if (!guard || m_stopping) return;
    if (m_target) reconcile(m_reconcile_turn);
    else if (m_incomplete && m_backend) m_backend->projection->request();
}
void CoinsListModel::setSearchText(const QString& v) { if (m_search != v) { m_search = v; rebuildView(); } }
void CoinsListModel::setSortBy(const QString& v) { if ((v == "date" || v == "amount" || v == "label") && m_sort != v) { m_sort = v; rebuildView(); } }
void CoinsListModel::setSortDescending(bool v) { if (m_descending != v) { m_descending = v; rebuildView(); } }
void CoinsListModel::setFilter(const QString& v) { if ((v == "all" || v == "spendable" || v == "locked") && m_filter != v) { m_filter = v; rebuildView(); } }
void CoinsListModel::setGroupBy(const QString& v) { if ((v == "date" || v == "month" || v == "address") && m_group != v) { m_group = v; rebuildView(); } }
void CoinsListModel::setMinAmount(qint64 v) { v = v < 0 ? -1 : v; if (m_min_amount != v) { m_min_amount = v; rebuildView(); } }
void CoinsListModel::setMaxAmount(qint64 v) { v = v < 0 ? -1 : v; if (m_max_amount != v) { m_max_amount = v; rebuildView(); } }
quint64 CoinsListModel::selectionCommand(std::optional<COutPoint> outpoint, SelectionMode mode, std::shared_ptr<const Selection> replacement)
{
    if (m_stopping || !m_backend || !m_opening->available() || m_selection_operations.size() >= 16) return 0;
    const auto ticket = m_selection_ticket + 1;
    const QPointer<CoinsListModel> guard{this};
    auto operation = std::make_shared<execution::OwnedCommand<SelectionResult>>(m_services.scheduler,
        [post = m_services.post, guard] { post([guard] { if (guard) guard->publishSelections(); }); });
    if (!operation->start([backend = m_backend, outpoint, mode, replacement] { return backend->select(outpoint, mode, replacement); },
        {execution::Lane::Domain, {}, execution::WorkClass::Action})) {
        m_read_error = tr("The coin selection request could not be queued."); Q_EMIT readErrorChanged(); return 0;
    }
    m_selection_ticket = ticket; ++m_selection_commands; m_last_selection = operation; m_selection_operations.push_back(std::move(operation));
    Q_EMIT selectionSnapshotChanged(); return ticket;
}
quint64 CoinsListModel::requestSelectionDelta(const COutPoint& outpoint, bool selected) { return selectionCommand(outpoint, selected ? SelectionMode::Select : SelectionMode::Unselect); }
quint64 CoinsListModel::requestReplaceSelection(std::shared_ptr<const Selection> replacement) { return selectionCommand({}, SelectionMode::Replace, std::move(replacement)); }
void CoinsListModel::publishSelections()
{
    if (m_stopping) return;
    const QPointer<CoinsListModel> guard{this};
    bool consumed{false};
    while (!m_selection_operations.empty()) {
        const auto result = m_selection_operations.front()->result(); if (!result) break;
        m_selection_operations.pop_front(); --m_selection_commands; consumed = true;
        if (result->error || !result->value || result->value->failed) m_read_error = tr("The coin selection could not be updated.");
        else if (result->value->revision >= m_selection_revision) {
            m_selection = result->value->selection; m_selection_revision = result->value->revision;
            m_input_mode = result->value->input_mode;
            m_selection_editing = result->value->editing;
        }
    }
    if (!consumed) return;
    Q_EMIT selectionSnapshotChanged(); if (!guard || m_stopping) return;
    Q_EMIT readErrorChanged();
}
void CoinsListModel::refreshSelection() { rebuildView(); }
void CoinsListModel::toggleCoinSelection(int row)
{
    if (m_stopping || !m_rows || row < 0 || row >= rowCount()) return;
    selectionCommand(m_rows->at(row)->value->outpoint, SelectionMode::Toggle);
}
bool CoinsListModel::toggleCoinSelectionById(const QString& id)
{
    if (m_stopping || !m_published_view) return false;
    const auto found = m_published_view->all_ids.find(id);
    return found != m_published_view->all_ids.end() && selectionCommand(found->second, SelectionMode::Toggle) != 0;
}
bool CoinsListModel::markCommand(std::optional<COutPoint> outpoint)
{
    if (m_stopping || !m_backend || !m_opening->available() || m_mark_operations.size() >= 16) return false;
    const QPointer<CoinsListModel> guard{this};
    auto operation = std::make_shared<execution::OwnedCommand<MarkOutcome>>(m_services.scheduler,
        [post = m_services.post, guard] { post([guard] { if (guard) guard->publishMarks(); }); });
    if (!operation->start([backend = m_backend, outpoint] { return backend->mark(outpoint); },
        {execution::Lane::Domain, {}, execution::WorkClass::Action})) return false;
    m_mark_operations.push_back(std::move(operation)); Q_EMIT marksChanged(); return true;
}
bool CoinsListModel::toggleMarkById(const QString& id)
{
    if (!m_published_view) return false;
    const auto found = m_published_view->all_ids.find(id);
    return found != m_published_view->all_ids.end() && markCommand(found->second);
}
bool CoinsListModel::clearMarks() { return markCommand({}); }
void CoinsListModel::publishMarks()
{
    if (m_stopping) return;
    bool consumed{false};
    while (!m_mark_operations.empty()) {
        const auto result = m_mark_operations.front()->result(); if (!result) break;
        m_mark_operations.pop_front(); consumed = true;
        if (result->error || !result->value || result->value->failed) m_read_error = tr("Coin marks could not be updated.");
        else if (result->value->revision >= m_mark_revision) {
            m_mark_revision = result->value->revision; m_marked_count = result->value->marks->size();
        }
    }
    if (consumed) {
        const QPointer<CoinsListModel> guard{this};
        Q_EMIT marksChanged(); if (guard && !m_stopping) Q_EMIT readErrorChanged();
    }
}
void CoinsListModel::beginSelection()
{
    if (m_stopping || m_selection_editing) return;
    if (selectionCommand({}, SelectionMode::Begin)) m_selection_editing = true;
}
void CoinsListModel::applySelection()
{
    if (m_selection_editing && selectionCommand({}, SelectionMode::Apply)) m_selection_editing = false;
}
void CoinsListModel::cancelSelection()
{
    if (m_selection_editing && selectionCommand({}, SelectionMode::Cancel)) m_selection_editing = false;
}
bool CoinsListModel::setManualInputSelection(const bool manual)
{
    if (m_stopping || !m_backend || !m_opening || !m_opening->available()) return false;
    if (!selectionPending() && manual == manualInputSelection() &&
        (manual || !m_selection || m_selection->empty())) return true;
    return selectionCommand({}, manual ? SelectionMode::ManualInputs : SelectionMode::AutomaticInputs) != 0;
}
bool CoinsListModel::setCoinsLocked(const QStringList& ids, bool locked) { return lockCoins(ids, locked, false); }
bool CoinsListModel::lockMarkedCoins(bool locked) { return lockCoins({}, locked, true); }
bool CoinsListModel::lockCoins(const QStringList& ids, bool locked, bool marked)
{
    if (m_stopping || !m_backend || !m_opening->available() || (!marked && ids.empty()) || m_lock_commands.size() >= 16) return false;
    const auto action = m_opening->startAction(); if (!action) return false;
    const QPointer<CoinsListModel> guard{this};
    auto command = std::make_shared<execution::OwnedCommand<LockResult>>(m_services.scheduler,
        [post = m_services.post, guard] { post([guard] { if (guard) guard->publishLocks(); }); });
    if (!command->startWith([opening = m_opening, backend = m_backend, action, ids, locked, marked](auto done, auto failed) {
        return opening->postDomain([backend, action, ids, locked, marked, done = std::move(done), failed = std::move(failed)] {
            try { backend->enqueueLock(action, ids, locked, done, marked); } catch (...) { failed(std::current_exception()); }
        }, execution::WorkClass::Action);
    })) return false;
    if (m_lock_commands.empty()) m_lock_error.clear();
    m_last_lock = command; m_lock_commands.push_back({std::move(command)});
    Q_EMIT lockOperationChanged(); if (guard) Q_EMIT selectionSnapshotChanged();
    return true;
}
void CoinsListModel::publishLocks()
{
    if (m_stopping) return;
    const QPointer<CoinsListModel> guard{this}; bool consumed{false};
    while (!m_lock_commands.empty()) {
        const auto result = m_lock_commands.front().operation->result(); if (!result) break;
        m_lock_commands.pop_front(); consumed = true;
        if (result->value && result->value->selection && result->value->selection_revision >= m_selection_revision) {
            m_selection = result->value->selection; m_selection_revision = result->value->selection_revision;
        }
        if (result->value && result->value->marks && result->value->marks_revision >= m_mark_revision) {
            m_mark_revision = result->value->marks_revision; m_marked_count = result->value->marks->size();
        }
        if (result->error || !result->value || result->value->uncertain) m_lock_error = tr("A coin lock action ended without a confirmed outcome. Refresh before retrying.");
        else if (result->value->already_locked) m_lock_error = tr("Some coins were already locked. Unlock them before requesting a persistent lock.");
        else if (result->value->failed) m_lock_error = tr("Some coin lock changes could not be saved. Refresh before retrying.");
    }
    if (!consumed) return;
    if (m_lock_commands.empty() && m_wallet_model) m_wallet_model->scheduleFeeEstimates();
    Q_EMIT lockOperationChanged(); if (!guard || m_stopping) return;
    Q_EMIT marksChanged(); if (guard && !m_stopping) Q_EMIT selectionSnapshotChanged();
}

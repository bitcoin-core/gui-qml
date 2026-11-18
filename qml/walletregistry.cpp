// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/walletregistry.h>
#include <qml/backendexecutor.h>
#include <qml/retainedsnapshot.h>
#include <QCoreApplication>
#include <QMutexLocker>
#include <QScopeGuard>
#include <algorithm>
#include <array>
#include <utility>

struct OpenWalletListModel::State {
    struct Node {
        uint64_t key{0};
        WalletQmlModel* wallet{nullptr};
        Node* left{nullptr};
        Node* right{nullptr};
        int height{1};
        int size{1};
    };
    WalletNameRegistry registry;
    std::deque<Node> arena;
    std::deque<Node*> free;
    std::map<WalletQmlModel*, uint64_t> keys;
    Node* root{nullptr};
    uint64_t next{0};
    bool transaction{false};
    bool closed{false};
    bool reconciling{false};
    bool scheduled{false};
    WalletQmlModel* inserting{nullptr};
    std::set<WalletQmlModel*> removals;
    std::map<WalletQmlModel*, QPointer<WalletQmlModel>> observers;
    static int Size(const Node* node) { return node ? node->size : 0; }
    static int Height(const Node* node) { return node ? node->height : 0; }
    static void UpdateMetadata(Node* node)
    {
        node->size = 1 + Size(node->left) + Size(node->right);
        node->height = 1 + std::max(Height(node->left), Height(node->right));
    }
    static Node* RotateLeft(Node* node)
    {
        auto* replacement = node->right;
        node->right = replacement->left;
        replacement->left = node;
        UpdateMetadata(node);
        UpdateMetadata(replacement);
        return replacement;
    }
    static Node* RotateRight(Node* node)
    {
        auto* replacement = node->left;
        node->left = replacement->right;
        replacement->right = node;
        UpdateMetadata(node);
        UpdateMetadata(replacement);
        return replacement;
    }
    static Node* Rebalance(Node* node)
    {
        UpdateMetadata(node);
        if (Height(node->left) > Height(node->right) + 1) {
            if (Height(node->left->right) > Height(node->left->left)) {
                node->left = RotateLeft(node->left);
            }
            return RotateRight(node);
        }
        if (Height(node->right) > Height(node->left) + 1) {
            if (Height(node->right->left) > Height(node->right->right)) {
                node->right = RotateRight(node->right);
            }
            return RotateLeft(node);
        }
        return node;
    }
    static Node* Insert(Node* root, Node* node) {
        // An AVL tree with an int row count has height below 64.
        std::array<Node**, 64> path{};
        size_t depth{0};
        auto** link = &root;
        while (*link) {
            Q_ASSERT(depth < path.size());
            path[depth++] = link;
            link = node->key < (*link)->key ? &(*link)->left : &(*link)->right;
        }
        *link = node;
        while (depth) {
            auto** ancestor = path[--depth];
            *ancestor = Rebalance(*ancestor);
        }
        return root;
    }
    static Node* Remove(Node* root, uint64_t key, Node*& removed) {
        std::array<Node**, 64> path{};
        size_t depth{0};
        auto** link = &root;
        while (key != (*link)->key) {
            Q_ASSERT(depth < path.size());
            path[depth++] = link;
            link = key < (*link)->key ? &(*link)->left : &(*link)->right;
        }
        if ((*link)->left && (*link)->right) {
            auto* target = *link;
            Q_ASSERT(depth < path.size());
            path[depth++] = link;
            link = &target->right;
            while ((*link)->left) {
                Q_ASSERT(depth < path.size());
                path[depth++] = link;
                link = &(*link)->left;
            }
            std::swap(target->key, (*link)->key);
            std::swap(target->wallet, (*link)->wallet);
        }
        removed = *link;
        *link = removed->left ? removed->left : removed->right;
        while (depth) {
            auto** ancestor = path[--depth];
            *ancestor = Rebalance(*ancestor);
        }
        return root;
    }
    static const Node* At(const Node* root, int row) {
        while (root) {
            const int left_size = Size(root->left);
            if (row == left_size) return root;
            if (row < left_size) {
                root = root->left;
            } else {
                row -= left_size + 1;
                root = root->right;
            }
        }
        return nullptr;
    }
    int Rank(uint64_t key) const {
        int row{0};
        const auto* node = root;
        while (node) {
            if (key == node->key) return row + Size(node->left);
            if (key < node->key) {
                node = node->left;
            } else {
                row += Size(node->left) + 1;
                node = node->right;
            }
        }
        return -1;
    }
};
OpenWalletListModel::OpenWalletListModel(std::shared_ptr<execution::Scheduler> scheduler, QObject* parent, presentation::Post post)
    : QAbstractListModel(parent), m_post(presentation::ReliablePost(post ? std::move(post) : presentation::QueuedPost()))
{
    m_state = execution::RetainSnapshot<State>(scheduler, [] { return std::make_unique<State>(); });
}
std::shared_ptr<WalletNameRegistry> OpenWalletListModel::nameRegistry() const
{
    return m_state ? std::shared_ptr<WalletNameRegistry>{m_state, &m_state->registry} : std::shared_ptr<WalletNameRegistry>{};
}
OpenWalletListModel::~OpenWalletListModel() { close(); }
void OpenWalletListModel::close()
{
    if (!m_state) return;
    m_state->closed = true;
    if (!m_state->transaction) m_state.reset();
}
int OpenWalletListModel::rowCount(const QModelIndex& parent) const { return parent.isValid() || !m_state ? 0 : State::Size(m_state->root); }
QModelIndex OpenWalletListModel::index(int row, int column, const QModelIndex& parent) const
{
    if (parent.isValid() || column != 0 || row < 0 || row >= rowCount()) return {};
    return createIndex(row, column, State::At(m_state->root, row)->wallet);
}
QVariant OpenWalletListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.model() != this || index.row() < 0 || index.row() >= rowCount() || role != Qt::UserRole + 1) return {};
    const auto found = m_state->observers.find(static_cast<WalletQmlModel*>(index.internalPointer()));
    return found == m_state->observers.end() ? QVariant{} : QVariant::fromValue(found->second.data());
}
QHash<int, QByteArray> OpenWalletListModel::roleNames() const { return {{Qt::UserRole + 1, "wallet"}}; }
int OpenWalletListModel::rowForWallet(WalletQmlModel* wallet) const
{
    if (!m_state) return -1;
    const auto key = m_state->keys.find(wallet); return key == m_state->keys.end() ? -1 : m_state->Rank(key->second);
}
bool OpenWalletListModel::addWallet(WalletQmlModel* wallet)
{
    if (!m_state || m_state->closed || m_state->transaction || !wallet || !wallet->available()) return false;
    if (m_state->keys.contains(wallet)) return true;
    const int row = rowCount();
    if (row == std::numeric_limits<int>::max() || m_state->next == std::numeric_limits<uint64_t>::max()) return false;
    auto state = m_state;
    const QPointer<OpenWalletListModel> guard{this};
    const QPointer<WalletQmlModel> original{wallet};
    state->transaction = true;
    state->inserting = wallet;
    beginInsertRows({}, row, row);
    if (!guard) return false;
    State::Node* node;
    if (state->free.empty()) {
        state->arena.emplace_back();
        node = &state->arena.back();
    } else {
        node = state->free.front();
        state->free.pop_front();
        *node = {};
    }
    node->key = ++state->next;
    node->wallet = wallet;
    state->keys.emplace(wallet, node->key);
    state->observers.emplace(wallet, original);
    state->root = State::Insert(state->root, node);
    endInsertRows();
    if (!guard) return false;
    state->inserting = nullptr;
    state->transaction = false;
    if (state->closed) {
        m_state.reset();
        return false;
    }
    if (!original || !original->available()) state->removals.insert(wallet);
    reconcile();
    return guard && m_state == state && state->keys.contains(wallet);
}
void OpenWalletListModel::removeWallet(WalletQmlModel* wallet)
{
    const QPointer<OpenWalletListModel> guard{this};
    const bool removed = removeWalletRow(wallet);
    if (guard && removed) reconcile();
}
bool OpenWalletListModel::removeWalletRow(WalletQmlModel* wallet)
{
    if (!m_state || m_state->closed) return false;
    if (m_state->transaction) {
        // Only a live row or the single promised insertion can enter this
        // inventory; foreign pointers cannot grow a deferred-removal queue.
        if (m_state->keys.contains(wallet) || m_state->inserting == wallet) m_state->removals.insert(wallet);
        return false;
    }
    const int row = rowForWallet(wallet);
    if (row < 0) return false;
    auto state = m_state;
    const auto key = state->keys.at(wallet);
    const QPointer<OpenWalletListModel> guard{this};
    state->transaction = true;
    beginRemoveRows({}, row, row);
    if (!guard) return false;
    State::Node* removed{nullptr};
    state->root = State::Remove(state->root, key, removed);
    state->keys.erase(wallet);
    state->observers.erase(wallet);
    state->removals.erase(wallet);
    removed->wallet = nullptr;
    state->free.push_back(removed);
    endRemoveRows();
    if (!guard) return false;
    state->transaction = false;
    if (state->closed) {
        m_state.reset();
        return false;
    }
    // The caller owns reconciliation, so the bounded drain does not recurse.
    return true;
}
void OpenWalletListModel::reconcile()
{
    if (!m_state || m_state->closed || m_state->transaction || m_state->reconciling) return;
    const auto state = m_state;
    const QPointer<OpenWalletListModel> guard{this};
    state->reconciling = true;
    for (int budget = 32; budget > 0 && !state->removals.empty(); --budget) {
        auto* wallet = *state->removals.begin();
        state->removals.erase(state->removals.begin());
        removeWalletRow(wallet);
        if (!guard || m_state != state) return;
    }
    state->reconciling = false;
    if (!state->removals.empty() && !state->scheduled) {
        state->scheduled = true;
        m_post([guard, state] {
            state->scheduled = false;
            if (guard && guard->m_state == state) guard->reconcile();
        });
    }
}

void WalletRegistry::Retirement::pruneAdoptions()
{
    if (!values) return;
    const uint64_t oldest = std::min(values->admitting_origin, values->origins.empty() ? std::numeric_limits<uint64_t>::max() : *values->origins.begin());
    int budget{32};
    while (budget-- > 0 && !values->close_order.empty()) {
        const auto next = values->close_order.begin();
        if (next->first.first >= oldest) break;
        const auto found = values->closed.find(next->second->name);
        if (found != values->closed.end() && found->second == next->second) values->closed.erase(found);
        values->close_order.erase(next);
    }
    if (!values->close_order.empty() && values->close_order.begin()->first.first < oldest) {
        const auto self = shared_from_this();
        post([self] { self->pruneAdoptions(); });
    }
}

void WalletRegistry::Retirement::watch(WalletQmlModel* model)
{
    if (!model || !values || values->alive.contains(model)) return;
    values->alive.emplace(model, model);
    ++native_backends;
    const auto weak = weak_from_this();
    model->backendExecutor()->whenDrained(bridge.get(), [weak] { if (const auto state = weak.lock()) { --state->native_backends; state->finish(); } });
    QObject::connect(model, &QObject::destroyed, bridge.get(), [weak, model] {
        if (const auto state = weak.lock(); state && state->values) {
            state->values->alive.erase(model); state->values->retiring.erase(model); state->values->adoptions.erase(model); state->finish();
        }
    });
}

bool WalletRegistry::Retirement::adopt(WalletQmlModel* model, bool remove)
{
    if (!model || !values || values->retiring.contains(model)) return false;
    if (!values->alive.contains(model)) watch(model);
    const bool target = observer && host->forgetCloseTarget(model);
    values->retiring.insert(model);
    const auto name = model->name();
    if (model != empty) values->retiring_names.insert(name);
    const auto self = shared_from_this();
    const QPointer<WalletQmlModel> original{model};
    const auto completed = std::make_shared<std::atomic_bool>(false);
    const auto shutdown_done = std::make_shared<bool>(false); // GUI bridge only.
    const auto found_adoption = values->adoptions.find(model);
    const std::weak_ptr<OpeningAdoption> weak_adoption = found_adoption == values->adoptions.end()
        ? std::weak_ptr<OpeningAdoption>{} : found_adoption->second;
    const auto retired = [self, original, model, name, completed, shutdown_done, weak_adoption] {
        const auto adoption = weak_adoption.lock();
        if (!*shutdown_done || (adoption && !adoption->pending_results.empty())) return;
        if (completed->exchange(true) || !self->values) return;
        if (adoption) adoption->retirement_ready = {};
        self->values->retiring.erase(model);
        const auto retiring_name = self->values->retiring_names.find(name);
        if (retiring_name != self->values->retiring_names.end()) self->values->retiring_names.erase(retiring_name);
        if (original && model != self->empty) original->deleteLater();
        self->finish();
    };
    if (const auto adoption = weak_adoption.lock()) adoption->retirement_ready = retired;
    const auto drained = [shutdown_done, retired] { *shutdown_done = true; retired(); };
    QObject::connect(model, &WalletQmlModel::shutdownFinished, bridge.get(), drained);
    if (model->shutdownComplete()) post(drained);
    if (const auto opening = model->opening()) opening->retire(remove);
    return target;
}

void WalletRegistry::Retirement::enqueue()
{
    if (std::exchange(pumping, true)) return;
    const auto self = shared_from_this();
    post([self] { self->pumping = false; self->pump(); });
}

void WalletRegistry::Retirement::pump()
{
    if (!started || batches_done || !values) return;
    int budget{32};
    while (budget-- > 0) {
        if (!values->forms.empty()) {
            auto form = std::move(values->forms.begin()->second); values->forms.erase(values->forms.begin());
            if (form.owner) form.close();
            continue;
        }
        if (values->pending.empty()) break;
        auto* raw = values->pending.front(); values->pending.pop_front();
        const auto found = values->alive.find(raw);
        if (found == values->alive.end() || !found->second) continue;
        const QPointer<WalletQmlModel> model = found->second;
        const auto name = model->name();
        const bool target = adopt(model, false);
        model->beginShutdown(); // Actual original-opening retirement is retained independently.
        if (observer) {
            if (target) host->notice(Notice::CloseTarget);
            if (observer) host->loadState(name, false);
        }
    }
    if (!values->forms.empty() || !values->pending.empty()) { enqueue(); return; }
    batches_done = true;
    if (empty) { adopt(empty, false); empty->beginShutdown(); }
    const auto self = shared_from_this();
    const auto notified = std::make_shared<std::atomic_bool>(false);
    shutdown([self, notified] {
        self->post([self, notified] {
            if (notified->exchange(true)) return;
            self->native_done = true;
            if (self->observer) self->host->notice(Notice::NativeDrained);
            self->finish();
        });
    });
    finish();
}

void WalletRegistry::Retirement::finish()
{
    if (!started || !batches_done || native_backends || (values && !values->retiring.empty())) return;
    // The native cleanup reservation owns all large inventory containers.
    // Dropping its lease lets the executor finish only after teardown runs.
    values.reset();
    if (!native_done || std::exchange(completed, true)) return;
    if (empty) { if (observer) empty->setParent(observer); else empty->deleteLater(); }
    if (observer) host->notice(Notice::Retired);
    keep_alive.reset();
}

bool WalletRegistry::registerModel(WalletQmlModel* wallet_model, std::shared_ptr<OpeningAdoption> adoption)
{
    const auto registry_lifetime = shared_from_this();
    const QPointer<QObject> observer{m_host->owner};
    const QPointer<WalletQmlModel> original{wallet_model};
    if (!adoption) adoption = retain<OpeningAdoption>(m_services.scheduler, [] { return std::make_unique<OpeningAdoption>(); });
    if (!adoption || !observer || m_host->stopping() || !original) return false;
    adoption->facade = wallet_model;
    adoption->identity = wallet_model->opening()->identity();
    m_retirement->values->adoptions.emplace(wallet_model, std::move(adoption));
    m_retirement->watch(wallet_model); // Covers both initial handoff and notification-created originals.
    const std::weak_ptr<WalletRegistry> weak = registry_lifetime;
    const auto host = m_host;
    QObject::connect(wallet_model, &WalletQmlModel::availableChanged, observer, [weak, host, wallet_model] {
        if (const auto registry = weak.lock(); registry && host->owner) {
            if (wallet_model->available() && !host->stopping()) registry->m_open_wallet_model->addWallet(wallet_model);
            else {
                host->prepareForget(wallet_model);
                registry->m_open_wallet_model->removeWallet(wallet_model);
            }
        }
    });
    QObject::connect(wallet_model, &WalletQmlModel::walletUnloaded, observer,
        [weak, host, original, post = m_services.post] {
            post([weak, host, original] { if (const auto registry = weak.lock(); registry && host->owner && original) registry->removeModel(original, false); });
        });
    QObject::connect(wallet_model, &WalletQmlModel::closeConfirmed, observer, [weak, host, wallet_model] {
        if (const auto registry = weak.lock(); registry && host->owner) registry->removeModel(wallet_model, true);
    });
    QObject::connect(wallet_model, &WalletQmlModel::closeQuestionChanged, observer, [host, wallet_model] {
        if (host->owner) host->closeQuestionChanged(wallet_model);
    });
    return true;
}

void WalletRegistry::removeModel(WalletQmlModel* wallet_model, bool remove)
{
    const auto registry_lifetime = shared_from_this();
    if (!wallet_model || wallet_model == m_services.empty) {
        return;
    }

    const QPointer<QObject> observer{m_host->owner};
    m_host->prepareForget(wallet_model);
    closeOpening(wallet_model);
    if (!observer || m_host->stopping()) return;
    QString wallet_name;
    WalletQmlModel* next_selected_wallet{nullptr};
    {
        QMutexLocker locker(&m_wallets_mutex);
        const auto position = m_wallet_positions.find(wallet_model);
        if (position == m_wallet_positions.end()) {
            return;
        }

        wallet_name = wallet_model->name();
        next_selected_wallet = firstAvailable(wallet_model);
        m_wallets.erase(position->second); m_wallet_positions.erase(position); m_wallet_names.erase(wallet_name);
        if (m_names_registry) { std::lock_guard name_lock{m_names_registry->mutex}; m_names_registry->names.erase(wallet_name); }
        m_wallet_information.erase(wallet_model);
    }

    const bool loaded = !m_wallets.empty();
    const bool loaded_changed = m_host->commitLoaded(loaded);
    // Commit the cached projection and resource handoff before any observer
    // can remove or destroy this controller in a nested GUI delivery.
    const bool target_changed = adoptRetirement(wallet_model, remove);
    m_host->forget(wallet_model, next_selected_wallet);
    if (!observer) return;
    m_open_wallet_model->removeWallet(wallet_model); if (!observer) return;
    wallet_model->beginShutdown(remove); if (!observer) return;
    if (target_changed) { m_host->notice(Notice::CloseTarget); if (!observer) return; }
    m_host->notice(Notice::Inventory); if (!observer) return;
    m_host->loadState(wallet_name, false);
    if (!observer) return;
    if (loaded_changed) m_host->notice(Notice::LoadedChanged);
}

WalletQmlModel* WalletRegistry::adoptWallet(std::unique_ptr<interfaces::Wallet>& wallet, const QString& name)
{
    if (!wallet) return nullptr;
    return adoptWallet([&wallet](auto&) { return std::move(wallet); }, name, {}, 0, {});
}

WalletQmlModel* WalletRegistry::adoptWallet(walletpublication::TakeWallet take, const std::optional<QString>& captured_name,
                                          std::shared_ptr<AdoptionOrigin> origin, uint64_t ingress_sequence,
                                          walletopening::NativeEvents observation)
{
    const auto registry_lifetime = shared_from_this();
    if (m_host->stopping()) return nullptr;
    const QPointer<QObject> guard{m_host->owner};
    ++m_adoption_depth;
    const auto publication_guard = qScopeGuard([guard, registry_lifetime] {
        if (!guard) return;
        --registry_lifetime->m_adoption_depth;
        registry_lifetime->m_host->notice(Notice::PublicationsReleased);
    });
    const auto state = m_retirement;
    const auto values = state->values;
    const auto scheduler = m_services.scheduler;
    const auto publication = m_host->publicationId();
    const auto current = [&] {
        return guard && !m_host->stopping() && state->values == values && m_host->publicationCurrent(publication);
    };
    if (!captured_name) { m_host->loadError(QCoreApplication::translate("WalletQmlController", "Wallet information is unavailable.")); return nullptr; }
    const QString& name = *captured_name;
    if (!origin) origin = m_publishing_origin ? m_publishing_origin : m_notification_origin;
    if (!origin) { m_host->adoptionUnavailable(false); return nullptr; }
    const auto source_sequence = ingress_sequence ? ingress_sequence : origin->admission_event;
    std::shared_ptr<OpeningAdoption> previous_opening;
    if (const auto bound = origin->bindings.find(name); bound != origin->bindings.end()) previous_opening = bound->second->opening;
    if (!previous_opening) {
        if (const auto closed = values->closed.find(name); closed != values->closed.end()) previous_opening = closed->second->opening;
    }
    if (!previous_opening) {
        if (const auto named = m_wallet_names.find(name); named != m_wallet_names.end()) previous_opening = values->adoptions.at(named->second);
    }
    const auto unloaded = previous_opening ? previous_opening->unloaded->load() : 0;
    if (unloaded && source_sequence <= unloaded) { m_host->adoptionUnavailable(true); return nullptr; }
    if (ingress_sequence && unloaded && ingress_sequence > unloaded) {
        // This observes a new load AFTER the original public unload callback;
        // the GUI interest context is not a Core backend identity. Never alter
        // its old binding, which still protects delayed command results.
        if (previous_opening->facade && !previous_opening->facade->available()) {
            removeModel(previous_opening->facade, false);
            if (!current()) return nullptr;
        }
        auto fresh = newOrigin();
        if (!current()) return nullptr;
        if (!fresh) { m_host->adoptionUnavailable(false); return nullptr; }
        origin = std::move(fresh);
    }
    if (origin->unavailable || source_sequence <= m_adoption_unavailable_event_cutoff) { m_host->adoptionUnavailable(false); return nullptr; }
    if (const auto closed = values->closed.find(name); closed != values->closed.end() && source_sequence <= closed->second->event_cutoff) {
        m_host->adoptionUnavailable(true);
        return nullptr;
    }
    std::shared_ptr<OpeningAdoption> adoption;
    if (const auto found = origin->bindings.find(name); found != origin->bindings.end()) {
        adoption = found->second->opening;
        if (adoption->closed || !adoption->facade || !adoption->facade->available() || adoption->facade->opening()->identity() != adoption->identity) {
            m_host->adoptionUnavailable(true);
            return nullptr;
        }
    }
    WalletQmlModel* model{adoption ? adoption->facade.data() : nullptr};
    bool mismatched_opening{false};
    {
        QMutexLocker locker(&m_wallets_mutex);
        if (const auto found = m_wallet_names.find(name); found != m_wallet_names.end()) {
            mismatched_opening = model && model != found->second;
            model = found->second;
        }
    }
    if (mismatched_opening) { m_host->adoptionUnavailable(true); return nullptr; }
    if (!adoption && model) adoption = m_retirement->values->adoptions.at(model);
    if (!adoption) adoption = retain<OpeningAdoption>(scheduler, [observation] {
        auto value = std::make_unique<OpeningAdoption>();
        if (observation.unloaded) value->unloaded = observation.unloaded;
        return value;
    });
    if (!current()) return nullptr;
    if (!adoption) { origin->unavailable = true; m_host->adoptionUnavailable(false); return nullptr; }
    if (!origin->bindings.contains(name)) {
        auto binding = retain<AdoptionOrigin::Binding>(scheduler, [name, adoption] {
            auto value = std::make_unique<AdoptionOrigin::Binding>(); value->name = name; value->opening = adoption; return value;
        });
        if (!current()) return nullptr;
        if (!binding) { origin->unavailable = true; m_host->adoptionUnavailable(false); return nullptr; }
        origin->bindings.emplace(binding->name, binding);
    }
    if (observation.unloaded && observation.unloaded->load()) {
        // This exact notification's backend was unloaded before first GUI
        // publication. Retain its closed binding for delayed original results,
        // without constructing a selectable facade or issuing another unload.
        const auto unloaded = observation.unloaded->load();
        uint64_t empty{0}; adoption->unloaded->compare_exchange_strong(empty, unloaded);
        adoption->closed = true;
        if (model) removeModel(model, false);
        if (current()) m_host->adoptionUnavailable(true);
        return nullptr;
    }
    const bool added = !model;
    if (added) {
        std::unique_ptr<interfaces::Handler> handler;
        auto wallet = take ? take(handler) : walletopening::Source{};
        if (!std::visit([](const auto& source) { return bool(source); }, wallet)) {
            m_host->adoptionUnavailable(false);
            return nullptr;
        }
        model = m_host->createFacade(std::move(wallet), captured_name.value_or(QString{}), {m_adoption_clock, adoption->unloaded}, std::move(handler));
        m_retirement->watch(model);
        if (!registerModel(model, adoption)) {
            if (!current()) return nullptr;
            const bool target = adoptRetirement(model, false);
            model->beginShutdown(false);
            if (!current()) return nullptr;
            if (target) m_host->notice(Notice::CloseTarget);
            if (!current()) return nullptr;
            m_host->adoptionUnavailable(false);
            return nullptr;
        }
        m_host->information(model, Information::Subscribe);
        insert(model, name);
    }
    // Bind the original command's release fence before any row/info observer
    // can close this opening, including callback-before-result delivery.
    if (origin->result_lifetime) retainResult(model, origin->result_lifetime);
    const QPointer<WalletQmlModel> original{model};
    const auto still_adopted = [&] {
        return current() && original && !adoption->closed && original->available()
            && original->opening()->identity() == adoption->identity && m_wallet_positions.contains(original)
            && m_wallet_names.contains(name) && m_wallet_names.at(name) == original;
    };
    const auto keep_adopted = [&] {
        if (still_adopted()) return true;
        if (current()) m_host->adoptionUnavailable(true);
        return false;
    };
    const bool listed = m_open_wallet_model->addWallet(model);
    if (!keep_adopted()) return nullptr;
    if (!listed) {
        m_host->adoptionUnavailable(false);
        return nullptr;
    }
    m_host->information(model, Information::ApplyAlias);
    if (!keep_adopted()) return nullptr;
    if (added) {
        m_host->notice(Notice::Inventory);
        if (!keep_adopted()) return nullptr;
    }
    m_host->loadState(name, true);
    if (!keep_adopted()) return nullptr;
    m_host->information(model, Information::Initialize);
    if (!keep_adopted()) return nullptr;
    m_host->notice(Notice::Loaded);
    if (!keep_adopted()) return nullptr;
    m_host->notice(Notice::Found);
    if (!keep_adopted()) return nullptr;
    if (m_publishing_origin == origin) m_host->notice(Notice::AdoptionAvailable);
    return model;
}

void WalletRegistry::observeOpening(const std::shared_ptr<AdoptionOrigin>& source, const std::shared_ptr<AdoptionOrigin>& intent,
                                   const QString& name, WalletQmlModel* facade, uint64_t notification_sequence, uint64_t ingress_sequence)
{
    const auto registry_lifetime = shared_from_this();
    const QPointer<QObject> observer{m_host->owner};
    const auto values = m_retirement->values;
    const auto current = [&] { return observer && !m_host->stopping() && values && m_retirement->values == values && intent == m_command_origin; };
    if (!current() || !source || !intent || source->unavailable || intent->unavailable || !notification_sequence || !containsExact(facade)) return;
    const auto existing = intent->bindings.find(name);
    if (existing != intent->bindings.end() && (existing->second->opening->closed || existing->second->opening->facade != facade)) return;
    const auto found = source->bindings.find(name);
    auto binding = found != source->bindings.end() ? found->second : std::shared_ptr<AdoptionOrigin::Binding>{};
    auto opening = binding ? binding->opening : std::shared_ptr<OpeningAdoption>{};
    if (!opening || opening->facade != facade) {
        // A fresh notification can still carry the GUI context of an opening
        // whose Native unload was already observed. Only an intent admitted
        // AFTER that unload may bind the exact newly adopted facade.
        if (!opening) {
            const auto closed = values->closed.find(name);
            if (closed == values->closed.end()) return;
            opening = closed->second->opening;
        }
        const auto unloaded = opening->unloaded->load();
        if (!opening->closed || !unloaded || ingress_sequence <= unloaded || intent->admission_event <= unloaded) return;
        const auto actual = values->adoptions.find(facade);
        if (actual == values->adoptions.end() || actual->second == opening) return;
        opening = actual->second;
        binding = retain<AdoptionOrigin::Binding>(m_services.scheduler, [name, opening] {
            auto value = std::make_unique<AdoptionOrigin::Binding>(); value->name = name; value->opening = opening; return value;
        });
        if (!current()) return;
        if (!binding) { intent->unavailable = true; return; }
    }
    if (opening->closed || opening->unloaded->load() || opening->facade != facade
        || !facade->available() || facade->opening()->identity() != opening->identity) return;
    if (existing != intent->bindings.end() && existing->second->opening != opening) return;
    // Only an owned handoff that just passed the source origin's adoption
    // fences can associate another intent with this exact opening. Name alone
    // never replaces an earlier association, including a closed same-name one.
    if (existing != intent->bindings.end()) binding = existing->second;
    if (!binding->notification_sequence || notification_sequence < binding->notification_sequence) binding->notification_sequence = notification_sequence;
    if (existing == intent->bindings.end()) intent->bindings.emplace(name, binding);
}

std::shared_ptr<WalletRegistry::AdoptionOrigin> WalletRegistry::newOrigin()
{
    const auto registry_lifetime = shared_from_this();
    const QPointer<QObject> observer{m_host->owner};
    if (m_origin_sequence == std::numeric_limits<uint64_t>::max()) return {};
    const auto sequence = ++m_origin_sequence;
    const auto admission_event = m_adoption_clock->next();
    if (!admission_event) return {};
    const auto scheduler = m_services.scheduler;
    const auto state = m_retirement;
    const auto values = state->values;
    const auto post = m_services.post;
    const auto previous_admission = values->admitting_origin;
    values->admitting_origin = std::min(previous_admission, sequence);
    const auto admission_guard = qScopeGuard([state, values, previous_admission] {
        values->admitting_origin = previous_admission;
        if (state->values == values) state->pruneAdoptions();
    });
    // Keep unregister transport charged until GUI consumption. It owns only
    // metadata, never an idle original action capability or wallet borrower.
    auto registration = retain<uint64_t>(scheduler, [sequence] { return std::make_unique<uint64_t>(sequence); });
    if (!registration || !observer || m_host->stopping() || state->values != values) return {};
    const std::weak_ptr<Retirement> weak = state;
    auto origin = retain<AdoptionOrigin>(scheduler, [sequence, admission_event, weak, registration, post] {
        auto value = std::make_unique<AdoptionOrigin>();
        value->sequence = sequence;
        value->admission_event = admission_event;
        value->retired = [weak, registration, post] {
            post([weak, registration] {
                if (const auto owner = weak.lock(); owner && owner->values) {
                    owner->values->origins.erase(*registration);
                    owner->pruneAdoptions();
                }
            });
        };
        return value;
    });
    if (!observer || m_host->stopping() || state->values != values) return {};
    if (origin) values->origins.insert(sequence);
    return origin;
}

void WalletRegistry::closeOpening(WalletQmlModel* wallet)
{
    const auto registry_lifetime = shared_from_this();
    const QPointer<QObject> observer{m_host->owner};
    const auto state = m_retirement;
    const auto values = state->values;
    if (!values) return;
    const auto found = values->adoptions.find(wallet);
    if (found == values->adoptions.end() || found->second->closed) return;
    const auto opening = found->second;
    const auto unloaded = opening->unloaded->load();
    const auto observed = unloaded ? unloaded : m_adoption_clock->next();
    const auto cutoff = observed ? observed : std::numeric_limits<uint64_t>::max();
    const auto origin_cutoff = m_origin_sequence;
    const QString name = wallet->name();
    opening->closed = true; // Fence before allocation or any reentrant signal.
    m_host->prepareForget(wallet);
    wallet->close();
    if (!observer || m_host->stopping() || state->values != values) return;
    const auto scheduler = m_services.scheduler;
    std::shared_ptr<Retirement::ClosedFence> fence;
    if (m_close_fence_sequence != std::numeric_limits<uint64_t>::max()) {
        fence = retain<Retirement::ClosedFence>(scheduler, [name, origin_cutoff, cutoff, opening] {
            return std::make_unique<Retirement::ClosedFence>(Retirement::ClosedFence{name, origin_cutoff, cutoff, opening});
        });
    }
    if (!observer || m_host->stopping() || state->values != values) return;
    if (fence && m_close_fence_sequence != std::numeric_limits<uint64_t>::max()) {
        const auto latest = values->closed.find(name);
        if (latest == values->closed.end() || latest->second->event_cutoff <= cutoff) values->closed.insert_or_assign(name, fence);
        values->close_order.emplace(std::pair{origin_cutoff, ++m_close_fence_sequence}, fence);
    } else {
        m_adoption_unavailable_event_cutoff = std::max(m_adoption_unavailable_event_cutoff, cutoff);
    }
    auto next_origin = newOrigin();
    if (!observer || m_host->stopping() || state->values != values) return;
    m_notification_origin = std::move(next_origin);
    m_host->contextChanged();
    if (observer && !m_host->stopping()) state->pruneAdoptions();
}

WalletRegistry::WalletRegistry(std::shared_ptr<Host> host, Services services)
    : m_host(std::move(host)), m_services(std::move(services))
{
    m_retirement = std::make_shared<Retirement>();
    m_retirement->shutdown = m_services.shutdown;
    m_retirement->bridge = m_services.bridge;
    m_retirement->observer = m_host->owner; m_retirement->host = m_host; m_retirement->empty = m_services.empty;
    m_retirement->post = m_services.post;
    m_retirement->values = execution::RetainSnapshot<Retirement::Values>(m_services.scheduler, [] { return std::make_unique<Retirement::Values>(); });
    if (!m_retirement->values) {
        // No catalog or form inventory may be admitted without retained cleanup.
        m_retirement->retained = false; m_retirement->values = std::make_shared<Retirement::Values>();
    }
    m_retirement->watch(m_services.empty);
    m_open_wallet_model = new OpenWalletListModel(m_services.scheduler, m_host->owner, m_services.post);
    m_names_registry = m_open_wallet_model->nameRegistry();
}

bool WalletRegistry::containsExact(WalletQmlModel* wallet) const
{
    return wallet && m_wallet_positions.contains(wallet);
}
WalletQmlModel* WalletRegistry::walletForName(const QString& name) const
{
    const auto found = m_wallet_names.find(name);
    return found != m_wallet_names.end() && found->second->available() ? found->second : nullptr;
}
WalletQmlModel* WalletRegistry::firstAvailable(WalletQmlModel* except) const
{
    if (!m_open_wallet_model) return nullptr;
    // The ordered row tree already excludes unavailable facades. Looking at
    // its first two rows avoids scanning the entire live opening inventory.
    for (int row = 0; row < 2; ++row) {
        const auto index = m_open_wallet_model->index(row);
        auto* wallet = m_open_wallet_model->data(index, Qt::UserRole + 1).value<WalletQmlModel*>();
        if (wallet != except && containsExact(wallet) && wallet->available()) return wallet;
    }
    return nullptr;
}
QList<QObject*> WalletRegistry::openWallets() const
{
    QMutexLocker locker(&m_wallets_mutex);
    return QList<QObject*>(m_wallets.begin(), m_wallets.end());
}
void WalletRegistry::rememberInformation(WalletQmlModel* wallet, WalletInformation information)
{
    if (containsExact(wallet)) m_wallet_information.insert_or_assign(wallet, std::move(information));
}
void WalletRegistry::insert(WalletQmlModel* wallet, const QString& name)
{
    QMutexLocker locker(&m_wallets_mutex);
    m_wallets.push_back(wallet); m_wallet_positions.emplace(wallet, std::prev(m_wallets.end())); m_wallet_names.insert_or_assign(name, wallet);
    if (m_names_registry) { std::lock_guard name_lock{m_names_registry->mutex}; m_names_registry->names.insert(name); }
}
void WalletRegistry::close()
{
    if (m_open_wallet_model) m_open_wallet_model->close();
    clearOrigins();
    m_names_registry.reset();
}
bool WalletRegistry::adoptRetirement(WalletQmlModel* model, bool remove)
{
    m_wallet_information.erase(model);
    return m_retirement->adopt(model, remove);
}
void WalletRegistry::detachPresentation()
{
    // Preserve the paired Qt row transaction if its callback destroys the owner.
    if (!m_open_wallet_model) return;
    m_open_wallet_model->setParent(nullptr);
    m_open_wallet_model->deleteLater();
    m_open_wallet_model = nullptr;
}
void WalletRegistry::detachController() { m_retirement->observer = nullptr; m_host->owner = nullptr; }
void WalletRegistry::beginRetirement()
{
    auto state = m_retirement; state->keep_alive = state; state->started = true;
    state->values->pending = std::move(m_wallets);
    state->values->forms = std::move(m_forms);
    state->values->names = std::move(m_wallet_names);
    state->values->positions = std::move(m_wallet_positions);
    state->values->information = std::move(m_wallet_information);
    m_services.scheduler->stopAcceptingWork();
    state->enqueue(); // Every original resource is escrowed before publication.
}
void WalletRegistry::registerForm(QObject* form, std::function<void()> close)
{
    m_forms.emplace(form, Form{form, std::move(close)});
}
void WalletRegistry::forgetForm(QObject* form)
{
    m_forms.erase(form);
    if (m_retirement->values) m_retirement->values->forms.erase(form);
}

void WalletRegistry::retainResult(WalletQmlModel* wallet, const std::shared_ptr<ResultLifetime>& lifetime)
{
    const auto state = m_retirement;
    if (!state->values || !lifetime) return;
    const auto found = state->values->adoptions.find(wallet);
    if (found == state->values->adoptions.end() || !found->second->pending_results.insert(lifetime).second) return;
    const std::weak_ptr<Retirement> weak = state;
    const std::weak_ptr<ResultLifetime> token = lifetime;
    lifetime->whenReleased([weak, token, wallet, post = m_services.post] {
        post([weak, token, wallet] {
            const auto owner = weak.lock();
            if (!owner || !owner->values) return;
            const auto found = owner->values->adoptions.find(wallet);
            if (found == owner->values->adoptions.end()) return;
            const auto adoption = found->second;
            if (const auto original = token.lock()) adoption->pending_results.erase(original);
            const auto ready = adoption->retirement_ready; // Completion clears its stored copy.
            if (ready) ready();
        });
    });
}

namespace {
struct RegistryCreationReservation final : walletpublication::CatalogReservation {
    RegistryCreationReservation(QString id, std::shared_ptr<walletpublication::Release> release, std::shared_ptr<walletpublication::NativeObservation> observed,
        std::weak_ptr<WalletRegistry> registry, std::shared_ptr<WalletRegistry::AdoptionOrigin> value)
        : CatalogReservation(std::move(id), std::move(release), std::move(observed)), owner(std::move(registry)), origin(std::move(value)) {}
    std::weak_ptr<WalletRegistry> owner;
    std::shared_ptr<WalletRegistry::AdoptionOrigin> origin;
};
}
std::shared_ptr<walletpublication::CatalogReservation> WalletRegistry::reserveCreation(const QString& operation_id)
{
    const auto lifetime = shared_from_this();
    if (!m_host->owner || m_host->stopping() || m_adoption_depth || !m_retirement->retained) return {};
    auto origin = newOrigin();
    if (!origin || !m_host->owner || m_host->stopping()) return {};
    auto observation = retain<walletpublication::NativeObservation>(m_services.scheduler, [clock = m_adoption_clock] { return std::make_unique<walletpublication::NativeObservation>(clock); });
    if (!observation) return {};
    auto release = std::make_shared<ResultLifetime>(); origin->result_lifetime = release;
    auto ticket = std::make_shared<RegistryCreationReservation>(operation_id, std::move(release), std::move(observation), weak_from_this(), origin);
    m_command_origin = std::move(origin);
    m_host->contextChanged();
    return ticket;
}
walletpublication::Result WalletRegistry::publishCreation(const std::shared_ptr<walletpublication::CatalogReservation>& opaque,
    walletpublication::TakeWallet take, const QString& wallet_id, walletopening::NativeEvents observation)
{
    return publishWithLifetime(opaque, std::move(take), wallet_id, std::move(observation), {});
}
walletpublication::Result WalletRegistry::publishWithLifetime(const std::shared_ptr<walletpublication::CatalogReservation>& opaque,
    walletpublication::TakeWallet take, const QString& wallet_id, walletopening::NativeEvents observation, std::shared_ptr<walletpublication::Release> cell)
{
    const auto lifetime = shared_from_this();
    const auto ticket = std::dynamic_pointer_cast<RegistryCreationReservation>(opaque);
    if (!ticket || ticket->owner.lock().get() != this || !m_host->owner || m_host->stopping() || m_adoption_depth) return {};
    // Notification-first adoption is already guarded by the aggregate token.
    // Attach the exact cell before releasing that aggregate, even if this
    // opening closed reentrantly before result publication.
    if (cell) {
        const auto binding = ticket->origin->bindings.find(wallet_id);
        if (binding != ticket->origin->bindings.end()) retainResult(binding->second->opening->facade, cell);
    }
    if (ticket->observation->failed() || (m_command_origin && m_command_origin != ticket->origin)) return {};
    m_command_origin = ticket->origin;
    const auto previous = exchangePublishingOrigin(ticket->origin);
    const auto restore = qScopeGuard([lifetime, previous] { lifetime->exchangePublishingOrigin(previous); });
    const auto original_lifetime = ticket->origin->result_lifetime;
    if (cell) ticket->origin->result_lifetime = cell;
    const auto restore_lifetime = qScopeGuard([ticket, original_lifetime] { ticket->origin->result_lifetime = original_lifetime; });
    auto* exact = take ? adoptWallet(std::move(take), wallet_id, ticket->origin, 0, std::move(observation)) : nullptr;
    if (exact && exact->available() && containsExact(exact)) return {exact, walletpublication::Availability::Available, {}};
    const auto binding = ticket->origin->bindings.find(wallet_id);
    if (binding != ticket->origin->bindings.end() && binding->second->opening->closed) {
        return {binding->second->opening->facade, walletpublication::Availability::Closed,
            QCoreApplication::translate("WalletCreationModel", "The wallet was created, then closed.")};
    }
    return {{}, walletpublication::Availability::Unavailable,
        QCoreApplication::translate("WalletCreationModel", "Wallet storage was created, but its view could not be published.")};
}
void WalletRegistry::finishCreation(const std::shared_ptr<walletpublication::CatalogReservation>& opaque)
{
    const auto lifetime = shared_from_this();
    const auto ticket = std::dynamic_pointer_cast<RegistryCreationReservation>(opaque);
    if (!ticket || ticket->owner.lock().get() != this || !m_host->owner || m_host->stopping()) return;
    if (m_command_origin != ticket->origin) return;
    m_command_origin.reset();
    m_notification_origin = newOrigin();
    if (m_host->owner && !m_host->stopping()) m_host->contextChanged();
}

// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/guipresentation.h>
#include <qml/models/addresslistmodel.h>

#include <addresstype.h>
#include <interfaces/wallet.h>
#include <key_io.h>
#include <qml/bitcoinunits.h>
#include <qml/models/bitcoinaddress.h>
#include <qml/models/walletqmlmodel.h>
#include <qml/retainedvalue.h>
#include <wallet/types.h>

#include <QVariantMap>
#include <QThread>
#include <algorithm>
#include <atomic>
#include <functional>
#include <map>
#include <deque>
#include <limits>
#include <set>
#include <stdexcept>

using wallet::AddressPurpose;

namespace {
QString CategoryName(AddressListModel::Category category)
{
    switch (category) {
    case AddressListModel::SingleUse: return QStringLiteral("single-use");
    case AddressListModel::Change: return QStringLiteral("change");
    }
    return {};
}

QString ScriptTypeName(const CTxDestination& destination)
{
    if (std::get_if<PKHash>(&destination)) return QStringLiteral("P2PKH");
    if (std::get_if<ScriptHash>(&destination)) return QStringLiteral("P2SH");
    if (std::get_if<WitnessV0KeyHash>(&destination)) return QStringLiteral("P2WPKH");
    if (std::get_if<WitnessV0ScriptHash>(&destination)) return QStringLiteral("P2WSH");
    if (std::get_if<WitnessV1Taproot>(&destination)) return QStringLiteral("P2TR");
    if (std::get_if<PayToAnchor>(&destination)) return QStringLiteral("P2A");
    if (std::get_if<WitnessUnknown>(&destination)) return QStringLiteral("Witness");
    if (std::get_if<PubKeyDestination>(&destination)) return QStringLiteral("P2PK");
    return {};
}

QString DisplayAmount(CAmount amount, int display_unit)
{
    const QmlBitcoinUnits::Unit unit{QmlBitcoinUnits::fromDisplayUnit(display_unit)};
    return QmlBitcoinUnits::formatForDisplay(unit, amount) + QStringLiteral(" ") + QmlBitcoinUnits::label(unit, amount);
}
} // namespace

struct AddressListModel::RowSlot { const AddressEntry* value{nullptr}; };
struct AddressListModel::Node {
    struct Arena { std::deque<RowSlot> slots; std::deque<AddressEntry> entries; };
    struct Row { RowSlot* slot; const AddressEntry* entry; std::shared_ptr<Arena> slot_owner, value_owner; } row;
    const Node* left{nullptr};
    const Node* right{nullptr};
    int height{1}, size{1};
};
struct AddressListModel::Projection {
    enum class Kind { Insert, Remove, Change };
    struct Step { Kind kind; int row; const Node* root; RowSlot* slot; const AddressEntry* entry; QList<int> roles; };
    std::deque<Node> nodes;
    std::vector<Step> steps;
    std::shared_ptr<Node::Arena> arena;
    const Node* initial{nullptr};
    const Node* final{nullptr};
    static int Size(const Node* root) { return root ? root->size : 0; }
    static int Height(const Node* root) { return root ? root->height : 0; }
    static const Node* At(const Node* root, int row) {
        while (root) {
            const int left = Size(root->left);
            if (row == left) return root;
            if (row < left) root = root->left;
            else { row -= left + 1; root = root->right; }
        }
        return nullptr;
    }
    const Node* Make(Node::Row row, const Node* left = nullptr, const Node* right = nullptr) {
        nodes.push_back({std::move(row), left, right, 1 + std::max(Height(left), Height(right)), 1 + Size(left) + Size(right)});
        return &nodes.back();
    }
    const Node* Balance(Node::Row row, const Node* left, const Node* right) {
        if (Height(left) > Height(right) + 1) {
            if (Height(left->left) >= Height(left->right)) return Make(left->row, left->left, Make(row, left->right, right));
            const auto* middle = left->right;
            return Make(middle->row, Make(left->row, left->left, middle->left), Make(row, middle->right, right));
        }
        if (Height(right) > Height(left) + 1) {
            if (Height(right->right) >= Height(right->left)) return Make(right->row, Make(row, left, right->left), right->right);
            const auto* middle = right->left;
            return Make(middle->row, Make(row, left, middle->left), Make(right->row, middle->right, right->right));
        }
        return Make(std::move(row), left, right);
    }
    struct Ancestor { const Node* node; bool left; };
    const Node* RebuildPath(const std::vector<Ancestor>& path, const Node* result, bool rebalance = true) {
        for (auto it = path.rbegin(); it != path.rend(); ++it) {
            const auto* node = it->node;
            const auto* left = it->left ? result : node->left;
            const auto* right = it->left ? node->right : result;
            result = rebalance ? Balance(node->row, left, right) : Make(node->row, left, right);
        }
        return result;
    }
    const Node* Insert(const Node* root, int position, Node::Row row) {
        std::vector<Ancestor> path;
        path.reserve(Height(root));
        while (root) {
            const int before = Size(root->left);
            const bool left = position <= before;
            path.push_back({root, left});
            if (!left) position -= before + 1;
            root = left ? root->left : root->right;
        }
        return RebuildPath(path, Make(std::move(row)));
    }
    const Node* Remove(const Node* root, int position) {
        std::vector<Ancestor> path;
        path.reserve(Height(root));
        for (;;) {
            const int before = Size(root->left);
            if (position == before) break;
            const bool left = position < before;
            path.push_back({root, left});
            if (!left) position -= before + 1;
            root = left ? root->left : root->right;
        }
        const Node* result{nullptr};
        if (!root->left) result = root->right;
        else if (!root->right) result = root->left;
        else {
            std::vector<Ancestor> successor_path;
            successor_path.reserve(Height(root->right));
            const auto* successor = root->right;
            while (successor->left) {
                successor_path.push_back({successor, true});
                successor = successor->left;
            }
            const auto* right = RebuildPath(successor_path, successor->right);
            result = Balance(successor->row, root->left, right);
        }
        return RebuildPath(path, result);
    }
    const Node* Replace(const Node* root, int position, Node::Row row) {
        std::vector<Ancestor> path;
        path.reserve(Height(root));
        for (;;) {
            const int before = Size(root->left);
            if (position == before) break;
            const bool left = position < before;
            path.push_back({root, left});
            if (!left) position -= before + 1;
            root = left ? root->left : root->right;
        }
        return RebuildPath(path, Make(std::move(row), root->left, root->right), false);
    }
    Node::Row Fresh(const AddressEntry& entry, const Node::Row* previous = nullptr) {
        if (!arena || arena->slots.size() + arena->entries.size() >= 256) arena = std::make_shared<Node::Arena>();
        arena->entries.push_back(entry);
        const auto* value = &arena->entries.back();
        if (previous) return {previous->slot, value, previous->slot_owner, arena};
        arena->slots.push_back({value});
        return {&arena->slots.back(), value, arena, arena};
    }
    const Node* Clone(const Node* old) {
        struct Work { const Node* node; bool expanded; };
        std::vector<Work> work;
        work.reserve(2 * Height(old) + 1);
        work.push_back({old, false});
        std::vector<const Node*> built;
        built.reserve(Height(old) + 1);
        while (!work.empty()) {
            const auto next = work.back(); work.pop_back();
            if (!next.node) { built.push_back(nullptr); continue; }
            if (next.expanded) {
                const auto* right = built.back(); built.pop_back();
                const auto* left = built.back(); built.pop_back();
                built.push_back(Make(next.node->row, left, right));
            } else {
                work.push_back({next.node, true});
                work.push_back({next.node->right, false});
                work.push_back({next.node->left, false});
            }
        }
        return built.back();
    }
    static QList<int> Roles(const AddressEntry& before, const AddressEntry& after) {
        QList<int> roles;
        if (before.label != after.label) roles.append(LabelRole);
        if (before.category != after.category) roles.append(CategoryRole);
        if (before.used != after.used) roles.append(UsedRole);
        if (before.current_balance != after.current_balance) roles.append(HasAmountRole);
        if (before.balance_text != after.balance_text) roles.append(CurrentBalanceRole);
        if (before.display_amount != after.display_amount) roles.append(DisplayAmountRole);
        if (before.script_type != after.script_type) roles.append(ScriptTypeRole);
        if (before.can_edit_label != after.can_edit_label) roles.append(CanEditLabelRole);
        if (before.can_create_payment_request != after.can_create_payment_request) roles.append(CanCreatePaymentRequestRole);
        return roles;
    }
    void Build(const Node* old, const std::vector<AddressEntry>& entries) {
        if (entries.size() > size_t(std::numeric_limits<int>::max())) throw std::length_error("Too many addresses for Qt model rows");
        initial = Clone(old);
        auto* root = initial;
        int row{0};
        for (const auto& entry : entries) {
            while (row < Size(root) && At(root, row)->row.entry->address < entry.address) {
                root = Remove(root, row); steps.push_back({Kind::Remove, row, root, nullptr, nullptr, {}});
            }
            const auto* previous = At(root, row);
            if (!previous || previous->row.entry->address != entry.address) {
                if (Size(root) == std::numeric_limits<int>::max()) throw std::length_error("Too many address transition rows");
                const auto next = Fresh(entry);
                root = Insert(root, row, next); steps.push_back({Kind::Insert, row, root, next.slot, next.entry, {}});
            } else if (*previous->row.entry != entry) {
                const auto next = Fresh(entry, &previous->row);
                auto roles = Roles(*previous->row.entry, entry);
                root = Replace(root, row, next); steps.push_back({Kind::Change, row, root, next.slot, next.entry, std::move(roles)});
            }
            ++row;
        }
        while (row < Size(root)) { root = Remove(root, row); steps.push_back({Kind::Remove, row, root, nullptr, nullptr, {}}); }
        final = root;
    }
};

// One original-opening metadata read, then bounded pure Domain preparation.
// Both the job and its output reserve Native retirement before acquiring values.
struct AddressListModel::LabelJob {
    struct CloneWork { const Node* node; bool expanded; };
    enum class Stage { Addresses, Clone, Labels } stage{Stage::Addresses};
    std::shared_ptr<walletopening::Session> opening;
    std::shared_ptr<const Projection> previous;
    const Node* root{nullptr};
    std::shared_ptr<Projection> projection;
    std::vector<interfaces::WalletAddress> addresses;
    std::map<QString, QString> labels;
    std::vector<CloneWork> clone;
    std::vector<const Node*> built;
    size_t address_index{0};
    int row{0};
    std::atomic_bool closed{false};
    bool finished{false};
    std::function<void(std::shared_ptr<const Projection>, std::exception_ptr)> notify;

    static void Finish(const std::shared_ptr<LabelJob>& self, std::exception_ptr error = {})
    {
        if (std::exchange(self->finished, true)) return;
        std::shared_ptr<const Projection> projection = error ? nullptr : self->projection;
        self->notify(std::move(projection), std::move(error));
    }
    static void Schedule(const std::shared_ptr<LabelJob>& self)
    {
        if (self->opening->postDomain([self] { Advance(self); })) return;
        Finish(self, std::make_exception_ptr(execution::NotStarted{execution::NotStarted::Cause::Transport}));
    }
    static void Advance(const std::shared_ptr<LabelJob>& self)
    {
        if (self->finished) return;
        if (self->closed.load() || !self->opening->observationOpen()) {
            Finish(self, std::make_exception_ptr(execution::NotStarted{execution::NotStarted::Cause::Cancelled})); return;
        }
        try {
            int budget{128};
            while (budget > 0 && self->stage == Stage::Addresses && self->address_index < self->addresses.size()) {
                --budget;
                const auto& address = self->addresses[self->address_index++];
                self->labels[QString::fromStdString(EncodeDestination(address.dest))] = QString::fromStdString(address.name);
            }
            if (self->stage == Stage::Addresses && self->address_index == self->addresses.size()) {
                self->clone.reserve(2 * Projection::Height(self->root) + 1);
                self->built.reserve(Projection::Height(self->root) + 1);
                self->clone.push_back({self->root, false}); self->stage = Stage::Clone;
            }
            while (budget > 0 && self->stage == Stage::Clone && !self->clone.empty()) {
                --budget;
                const auto work = self->clone.back(); self->clone.pop_back();
                if (!work.node) self->built.push_back(nullptr);
                else if (work.expanded) {
                    const auto* right = self->built.back(); self->built.pop_back();
                    const auto* left = self->built.back(); self->built.pop_back();
                    self->built.push_back(self->projection->Make(work.node->row, left, right));
                } else {
                    self->clone.push_back({work.node, true});
                    self->clone.push_back({work.node->right, false});
                    self->clone.push_back({work.node->left, false});
                }
            }
            if (self->stage == Stage::Clone && self->clone.empty()) {
                self->projection->initial = self->built.back();
                self->projection->final = self->projection->initial;
                self->stage = Stage::Labels;
            }
            while (budget > 0 && self->stage == Stage::Labels && self->row < Projection::Size(self->root)) {
                --budget;
                const int row = self->row++;
                const auto* old = Projection::At(self->root, row);
                const auto found = self->labels.find(old->row.entry->address);
                if (found == self->labels.end() || found->second == old->row.entry->label) continue;
                auto entry = *old->row.entry; entry.label = found->second;
                const auto next = self->projection->Fresh(entry, &old->row);
                self->projection->final = self->projection->Replace(self->projection->final, row, next);
                self->projection->steps.push_back({Projection::Kind::Change, row, self->projection->final,
                    next.slot, next.entry, {LabelRole}});
            }
            if (self->stage == Stage::Labels && self->row == Projection::Size(self->root)) Finish(self);
            else Schedule(self);
        } catch (...) { Finish(self, std::current_exception()); }
    }
    static void Start(const std::shared_ptr<LabelJob>& self)
    {
        const auto admitted = self->opening->read([self](interfaces::Wallet& wallet) {
            if (self->closed.load() || !self->opening->observationOpen())
                throw execution::NotStarted{execution::NotStarted::Cause::Cancelled};
            return wallet.getAddresses();
        },
            [self](std::vector<interfaces::WalletAddress> addresses) {
                self->addresses = std::move(addresses); Schedule(self);
            }, [self](std::exception_ptr error) { Finish(self, std::move(error)); });
        if (!admitted) Finish(self, std::make_exception_ptr(execution::NotStarted{execution::NotStarted::Cause::Transport}));
    }
};

AddressListModel::AddressListModel(WalletQmlModel* parent)
    : QAbstractListModel(parent)
    , m_wallet_model(parent)
{
    if (m_wallet_model) {
        connect(m_wallet_model, &WalletQmlModel::addressInventoryChanged, this, &AddressListModel::refresh);
        connect(m_wallet_model, &WalletQmlModel::addressLabelsChanged, this, &AddressListModel::refreshLabels);
        connect(m_wallet_model, &WalletQmlModel::walletContentsChanged, this, &AddressListModel::refresh);
    }
    refresh();
}

int AddressListModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) return 0;
    return Projection::Size(m_root);
}

int AddressListModel::count() const
{
    return Projection::Size(m_root);
}

QModelIndex AddressListModel::index(int row, int column, const QModelIndex& parent) const
{
    if (parent.isValid() || column != 0 || row < 0 || row >= count()) return {};
    return createIndex(row, column, Projection::At(m_root, row)->row.slot);
}
QVariant AddressListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.model() != this || index.row() < 0 || index.row() >= count()) return {};
    const auto* slot = static_cast<const RowSlot*>(index.internalPointer());
    if (!slot || !slot->value) return {};
    const auto& entry = *slot->value;
    switch (role) {
    case AddressRole: return entry.address;
    case FormattedAddressRole: return entry.formatted_address;
    case EllipsesAddressRole: return entry.ellipses_address;
    case LabelRole: return entry.label;
    case CategoryRole: return entry.category_text;
    case UsedRole: return entry.used;
    case CurrentBalanceRole: return entry.balance_text;
    case DisplayAmountRole: return entry.display_amount;
    case HasAmountRole: return entry.current_balance != 0;
    case ScriptTypeRole: return entry.script_type;
    case CanEditLabelRole: return entry.can_edit_label;
    case CanCreatePaymentRequestRole: return entry.can_create_payment_request;
    }
    return {};
}

QHash<int, QByteArray> AddressListModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[AddressRole] = "address";
    roles[FormattedAddressRole] = "formattedAddress";
    roles[EllipsesAddressRole] = "ellipsesAddress";
    roles[LabelRole] = "label";
    roles[CategoryRole] = "category";
    roles[UsedRole] = "isUsed";
    roles[CurrentBalanceRole] = "currentBalance";
    roles[DisplayAmountRole] = "displayAmount";
    roles[HasAmountRole] = "hasAmount";
    roles[ScriptTypeRole] = "scriptType";
    roles[CanEditLabelRole] = "canEditLabel";
    roles[CanCreatePaymentRequestRole] = "canCreatePaymentRequest";
    return roles;
}

AddressListModel::Category AddressListModel::category() const
{
    return m_category;
}

void AddressListModel::setCategory(Category category)
{
    if (m_stopping || m_category == category) return;
    ++m_query_generation;
    if (m_query) m_query->close();
    cancelLabelJob(); m_dirty = true;
    m_query.reset(); if (!m_notifying) m_pending.reset();
    m_category = category;
    const QPointer<AddressListModel> observer{this};
    Q_EMIT categoryChanged();
    if (observer) rebuild();
}

QVariantList AddressListModel::categoryOptions() const
{
    return {
        QVariantMap{{QStringLiteral("value"), static_cast<int>(SingleUse)}, {QStringLiteral("text"), tr("Receive")}},
        QVariantMap{{QStringLiteral("value"), static_cast<int>(Change)}, {QStringLiteral("text"), tr("Change")}},
    };
}

bool AddressListModel::showUsed() const
{
    return m_show_used;
}

void AddressListModel::setShowUsed(bool show_used)
{
    if (m_stopping || m_show_used == show_used) return;
    ++m_query_generation;
    if (m_query) m_query->close();
    cancelLabelJob(); m_dirty = true;
    m_query.reset(); if (!m_notifying) m_pending.reset();
    m_show_used = show_used;
    const QPointer<AddressListModel> observer{this};
    Q_EMIT showUsedChanged();
    if (observer) rebuild();
}

void AddressListModel::refresh()
{
    if (m_stopping) return;
    m_dirty = true;
    if (m_label_job || m_pending_labels) {
        ++m_query_generation; cancelLabelJob();
        m_pending.reset(); m_pending_labels = false;
    }
    if (m_notifying) return;
    if (m_pending) { applyTurn(m_query_generation); return; }
    rebuild();
}

void AddressListModel::refreshLabels()
{
    if (m_stopping) return;
    m_labels_dirty = true;
    if (m_pending_labels) {
        ++m_query_generation; m_pending.reset(); m_pending_labels = false;
    }
    if (!m_visible) m_dirty = true;
    rebuild();
}

bool AddressListModel::refreshPending() const
{
    return !m_stopping && (m_query || m_label_job || m_pending || (m_opening && (m_dirty || m_labels_dirty)));
}

void AddressListModel::notifyRefreshState()
{
    const bool pending = refreshPending();
    if (std::exchange(m_refresh_pending, pending) != pending) Q_EMIT refreshStateChanged();
}

void AddressListModel::cancelLabelJob()
{
    if (m_label_job) m_label_job->closed.store(true);
    m_label_job.reset();
}

void AddressListModel::setDisplayUnit(int unit)
{
    if (unit == m_display_unit || m_stopping) return;
    m_display_unit = unit;
    ++m_query_generation;
    if (m_query) m_query->close();
    cancelLabelJob(); m_dirty = true;
    m_query.reset(); if (!m_notifying) m_pending.reset();
    rebuild();
}

ReceiveRequestOperation* AddressListModel::setAddressLabel(const QString& address, const QString& label)
{
    if (!m_wallet_model || address.isEmpty()) return nullptr;
    return m_wallet_model->setAddressLabel(address, label);
}

TextEditSessionModel* AddressListModel::labelEditor(const QString& address) const
{
    return m_label_editors.value(address);
}
TextEditSessionModel* AddressListModel::editLabel(const QString& address)
{
    if (m_stopping || !m_wallet_model || address.isEmpty() || address.size() > 128) return nullptr;
    if (auto* held = labelEditor(address); held && held->phase() != TextEditSessionModel::Closed) return held;
    // addressId is the exact canonical identity published by this reader.
    // Lookup is bounded by the balanced tree height and never parses Core data.
    const Node* found = m_root;
    while (found && found->row.entry->address != address)
        found = address < found->row.entry->address ? found->left : found->right;
    if (!found || !found->row.entry->can_edit_label) return nullptr;
    for (auto it = m_label_editors.begin(); it != m_label_editors.end();) {
        if (!it.value() || it.value()->phase() == TextEditSessionModel::Closed) {
            if (it.value()) it.value()->deleteLater();
            it = m_label_editors.erase(it);
        } else ++it;
    }
    // Keep dirty drafts instead of evicting them to admit another editor.
    if (m_label_editors.size() >= 32) return nullptr;
    auto* editor = m_wallet_model->editAddressLabel(address, this);
    if (editor) {
        m_label_editors.insert(address, editor);
        connect(this, &QAbstractItemModel::dataChanged, editor, [address, editor](const QModelIndex& first, const QModelIndex& last, const QList<int>& roles) {
            if (!roles.isEmpty() && !roles.contains(LabelRole)) return;
            if (first == last && first.data(AddressRole).toString() != address) return;
            editor->refresh();
        });
    }
    return editor;
}

QString AddressListModel::addressAt(int row) const
{
    const auto* node = row < 0 || row >= count() ? nullptr : Projection::At(m_root, row);
    return node ? node->row.entry->address : QString{};
}
AddressListModel::~AddressListModel() { closeImpl(false); }
void AddressListModel::configureSession(std::shared_ptr<walletopening::Session> opening, presentation::Post post)
{
    attachSession(std::move(opening), std::move(post));
    rebuild();
}
void AddressListModel::attachSession(std::shared_ptr<walletopening::Session> opening, presentation::Post post)
{
    Q_ASSERT(!m_query && !m_visible);
    m_opening = std::move(opening); m_post = std::move(post);
    if (!m_post) {
        m_bridge = presentation::MakeGuiObject();
        m_post = [bridge = m_bridge](auto turn) { QMetaObject::invokeMethod(bridge.get(), std::move(turn), Qt::QueuedConnection); };
    }
    m_post = presentation::ReliablePost(std::move(m_post));
}
void AddressListModel::close()
{
    closeImpl(true);
}
void AddressListModel::closeImpl(bool notify)
{
    if (std::exchange(m_stopping, true)) return;
    for (const auto& editor : m_label_editors) if (editor) editor->retire();
    ++m_query_generation;
    if (m_query) m_query->close();
    cancelLabelJob(); m_dirty = false; m_labels_dirty = false; m_pending_labels = false;
    m_query.reset(); m_pending.reset();
    // Qt must observe the original row count throughout an open structural
    // notification. The active apply turn owns its frame until endRows.
    if (!m_notifying) { m_root = nullptr; m_visible.reset(); }
    if (notify) notifyRefreshState();
    else m_refresh_pending = false;
}
void AddressListModel::rebuild()
{
    if (m_stopping) return;
    if (m_notifying) return;
    if (!m_opening && m_wallet_model) {
        // A standalone wallet facade creates its child models before its
        // opening. Native ownership is established by initializeOpening(),
        // which refreshes this model after installing that original session.
        const auto opening = m_wallet_model->opening();
        if (!m_wallet_model->available() || !opening) return;
        attachSession(opening, {});
    }
    if (!m_opening || !m_opening->available()) return;
    if (m_query || m_label_job || m_pending) { notifyRefreshState(); return; }
    if (!m_dirty && !m_labels_dirty) { notifyRefreshState(); return; }
    if (!m_dirty && m_visible) { rebuildLabels(); return; }
    m_dirty = false; m_labels_dirty = false; m_pending_labels = false;
    const auto generation = ++m_query_generation;
    const QPointer<AddressListModel> observer{this};
    m_query = std::make_shared<execution::SnapshotQuery<std::shared_ptr<const Projection>>>(
        [opening = m_opening, previous = m_visible, root = m_root, category = m_category, show_used = m_show_used, unit = m_display_unit] {
            (void)previous; // Retains immutable source roots throughout Native planning.
            auto wallet = opening->borrow();
            if (!wallet) throw execution::NotStarted{execution::NotStarted::Cause::Cancelled};
            auto projection = walletopening::RetainValues<Projection>(opening, [&] {
                auto value = std::make_unique<Projection>();
                value->Build(root, collectEntries(*wallet, category, show_used, unit));
                return value;
            });
            if (!projection) throw std::runtime_error("Address projection is unavailable");
            return std::shared_ptr<const Projection>{std::move(projection)};
        }, [post = m_post, observer, generation] {
            post([observer, generation] { if (observer && observer->m_query_generation == generation) observer->publishSnapshot(); });
        }, m_opening->observationPost());
    const auto query = m_query;
    query->request();
    if (!observer || m_stopping || generation != m_query_generation) return;
    notifyRefreshState();
}
void AddressListModel::rebuildLabels()
{
    m_labels_dirty = false;
    const auto generation = ++m_query_generation;
    const QPointer<AddressListModel> observer{this};
    const auto opening = m_opening;
    const auto previous = m_visible;
    const auto* root = m_root;
    const auto post = m_post;
    auto job = walletopening::RetainValues<LabelJob>(opening, [] { return std::make_unique<LabelJob>(); });
    auto projection = walletopening::RetainValues<Projection>(opening, [] { return std::make_unique<Projection>(); });
    if (!job || !projection) {
        if (prepareLabelPublication(generation, true)) notifyRefreshState();
        return;
    }
    job->opening = opening; job->previous = previous; job->root = root; job->projection = std::move(projection);
    job->notify = [observer, generation, post](auto value, std::exception_ptr error) {
        post([observer, generation, value = std::move(value), error = std::move(error)]() mutable {
            if (observer) observer->publishLabels(std::move(value), std::move(error), generation);
        });
    };
    m_label_job = job;
    LabelJob::Start(job);
    if (!observer || m_stopping || generation != m_query_generation) return;
    notifyRefreshState();
}
bool AddressListModel::prepareLabelPublication(quint64 generation, bool failed)
{
    if (m_stopping || generation != m_query_generation || !m_opening || !m_opening->observationOpen()) return false;
    const QPointer<AddressListModel> observer{this};
    m_label_job.reset();
    if (!observer || m_stopping || generation != m_query_generation) return false;
    // An ACK after the captured metadata owns one fresh successor. Never
    // publish the superseded label snapshot or clear that successor's pending.
    if (m_dirty || m_labels_dirty) {
        // Preserve the dirty successor as pending, but start it on a fresh
        // presentation turn instead of recursing through capacity callbacks.
        m_post([observer, generation] {
            if (observer && !observer->m_stopping && observer->m_query_generation == generation) observer->rebuild();
        });
        return false;
    }
    const auto error = failed ? tr("Wallet address labels could not be refreshed.")
        : m_inventory_failed ? tr("Wallet addresses could not be refreshed.") : QString{};
    if (m_read_error != error) { m_read_error = error; Q_EMIT readErrorChanged(); }
    return observer && !m_stopping && generation == m_query_generation;
}
void AddressListModel::publishLabels(std::shared_ptr<const Projection> projection, std::exception_ptr failure, quint64 generation)
{
    if (!prepareLabelPublication(generation, bool(failure))) return;
    if (projection) {
        m_pending = std::move(projection); m_pending_step = 0; m_pending_labels = true;
        m_root = m_pending->initial; m_visible = m_pending;
        applyTurn(generation);
    } else notifyRefreshState();
}
void AddressListModel::publishSnapshot()
{
    if (m_stopping || !m_query || (m_opening && !m_opening->observationOpen())) return;
    const auto generation = m_query_generation;
    const auto result = m_query->take(); if (!result) return;
    m_query.reset();
    const QPointer<AddressListModel> observer{this};
    m_inventory_failed = bool(result->error);
    const auto error = result->error ? tr("Wallet addresses could not be refreshed.") : QString{};
    if (m_read_error != error) { m_read_error = error; Q_EMIT readErrorChanged(); }
    if (!observer || m_stopping || generation != m_query_generation) return;
    if (result->snapshot && *result->snapshot) {
        m_pending = *result->snapshot; m_pending_step = 0; m_pending_labels = false;
        m_root = m_pending->initial; m_visible = m_pending;
        applyTurn(m_query_generation);
    } else if (m_dirty || m_labels_dirty) rebuild();
    else notifyRefreshState();
}
void AddressListModel::applyTurn(quint64 generation)
{
    if (m_stopping || generation != m_query_generation || !m_pending || (m_opening && !m_opening->observationOpen())) return;
    const QPointer<AddressListModel> observer{this};
    const auto pending = m_pending;
    const int before = count();
    for (int budget = 0; budget < 32 && m_pending_step < pending->steps.size(); ++budget) {
        const auto& step = pending->steps[m_pending_step++];
        m_notifying = true;
        if (step.kind == Projection::Kind::Insert) {
            beginInsertRows({}, step.row, step.row); if (!observer) return;
            step.slot->value = step.entry; m_root = step.root;
            endInsertRows();
        } else if (step.kind == Projection::Kind::Remove) {
            beginRemoveRows({}, step.row, step.row); if (!observer) return;
            m_root = step.root; endRemoveRows();
        } else {
            step.slot->value = step.entry; m_root = step.root;
            Q_EMIT dataChanged(index(step.row), index(step.row), step.roles);
        }
        if (!observer) return;
        m_notifying = false;
        if (m_stopping) { m_root = nullptr; m_visible.reset(); return; }
        if (generation != m_query_generation) {
            m_pending.reset(); m_pending_labels = false;
            if (before != count()) { Q_EMIT countChanged(); if (!observer || m_stopping) return; }
            rebuild(); return;
        }
    }
    if (before != count()) { Q_EMIT countChanged(); if (!observer || m_stopping || generation != m_query_generation) return; }
    if (m_pending_step == pending->steps.size()) {
        m_pending.reset(); m_pending_labels = false;
        if (m_dirty || m_labels_dirty) rebuild();
        else notifyRefreshState();
    }
    else {
        try { m_post([observer, generation] { if (observer) observer->applyTurn(generation); }); }
        catch (...) { m_read_error = tr("Address presentation could not be delivered. Refresh to retry."); Q_EMIT readErrorChanged(); }
    }
}

std::vector<AddressListModel::AddressEntry> AddressListModel::collectEntries(
    interfaces::Wallet& wallet, Category category, bool show_used, int unit)
{
    const auto addresses = wallet.getAddresses();
    const auto coins = wallet.listCoins();
    const auto transactions = wallet.getWalletTxs();
    std::map<QString, CAmount> balances;
    std::map<QString, std::string> labels;
    std::set<QString> receive, used, change;
    std::set<COutPoint> change_outpoints;
    for (const auto& address : addresses) {
        const auto text = QString::fromStdString(EncodeDestination(address.dest));
        labels[text] = address.name;
        if (address.is_mine && address.purpose == AddressPurpose::RECEIVE) receive.insert(text);
    }
    for (const auto& tx : transactions) {
        for (size_t i = 0; i < tx.txout_address.size(); ++i) {
            if (i >= tx.txout_address_is_mine.size() || !tx.txout_address_is_mine[i]) continue;
            if (i < tx.txout_is_change.size() && tx.txout_is_change[i]) continue;
            const auto text = QString::fromStdString(EncodeDestination(tx.txout_address[i]));
            if (receive.contains(text)) used.insert(text);
        }
        if (tx.tx) {
            for (size_t i = 0; i < tx.txout_is_change.size(); ++i) {
                if (tx.txout_is_change[i]) change_outpoints.insert({tx.tx->GetHash(), static_cast<uint32_t>(i)});
            }
        }
    }
    for (const auto& [ancestor, outputs] : coins) {
        for (const auto& [outpoint, output] : outputs) {
            if (output.is_spent) continue;
            CTxDestination destination;
            if (!ExtractDestination(output.txout.scriptPubKey, destination)) continue;
            const auto text = QString::fromStdString(EncodeDestination(destination));
            if (text.isEmpty()) continue;
            if (!MoneyRange(output.txout.nValue) || output.txout.nValue > MAX_MONEY - balances[text]) throw std::runtime_error("Invalid address balance");
            balances[text] += output.txout.nValue;
            if (output.txout.nValue > 0 && change_outpoints.contains(outpoint)) change.insert(text);
        }
    }
    std::vector<AddressEntry> entries;
    if (category == Change) {
        for (const auto& text : change) {
            entries.push_back({text, QString::fromStdString(labels[text]), Change, used.contains(text),
                balances[text], ScriptTypeName(DecodeDestination(text.toStdString())), false, false, {}, {}, {}, {}, {}});
        }
    } else {
        for (const auto& address : addresses) {
            if (!address.is_mine || address.purpose != AddressPurpose::RECEIVE) continue;
            const auto text = QString::fromStdString(EncodeDestination(address.dest));
            if (text.isEmpty() || (!show_used && used.contains(text))) continue;
            entries.push_back({text, QString::fromStdString(address.name), SingleUse, used.contains(text),
                balances[text], ScriptTypeName(address.dest), true, true, {}, {}, {}, {}, {}});
        }
    }
    for (auto& entry : entries) {
        entry.formatted_address = BitcoinAddress::formattedAddress(entry.address);
        entry.ellipses_address = BitcoinAddress::ellipsesAddress(entry.address);
        entry.category_text = CategoryName(entry.category);
        entry.balance_text = QmlBitcoinUnits::format(QmlBitcoinUnits::fromDisplayUnit(unit), entry.current_balance);
        entry.display_amount = DisplayAmount(entry.current_balance, unit);
    }
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return a.address < b.address; });
    return entries;
}

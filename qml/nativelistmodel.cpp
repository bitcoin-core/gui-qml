// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/nativelistmodel.h>
#include <qml/asyncjoin.h>
#include <qml/executor.h>
#include <qml/guipresentation.h>
#include <qml/indexedsequence.h>
#include <QPointer>
#include <QThread>
#include <QScopeGuard>
#include <algorithm>
#include <deque>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
struct NativeListNode { NativeRowHandle row; const NativeListNode* left{nullptr}; const NativeListNode* right{nullptr}; int size{1}, height{1}; };
struct NativeListVersion { const NativeListNode* root{nullptr}; };
namespace {
int Size(const NativeListNode* node) { return node ? node->size : 0; }
int Height(const NativeListNode* node) { return node ? node->height : 0; }
void Collect(const NativeListNode* node, std::vector<NativeRowHandle>& rows) {
    std::vector<const NativeListNode*> path;
    path.reserve(Height(node));
    while (node || !path.empty()) {
        while (node) { path.push_back(node); node = node->left; }
        node = path.back(); path.pop_back(); rows.push_back(node->row); node = node->right;
    }
}
}
struct NativeListModel::Input { std::mutex mutex; NativeVersionHandle published; quint64 epoch{0}; };
struct NativeListModel::View {
    struct Slot { QString key; NativeRowHandle row; };
    std::map<QString, Slot> slots;
    IndexedSequence<Slot*> order;
};
struct NativeListModel::Plan {
    enum class Kind { Insert, Remove, Move, Change };
    struct Step { Kind kind; int from, to; QString key; NativeRowHandle row; size_t version; };
    std::weak_ptr<const NativeListVersion> base;
    bool empty_base{false}; quint64 epoch{0};
    std::vector<NativeRowHandle> target;
    std::deque<NativeListNode> nodes;
    std::deque<NativeListVersion> versions;
    std::vector<Step> steps;
    const NativeListNode* Make(NativeRowHandle row, const NativeListNode* left = nullptr, const NativeListNode* right = nullptr) {
        nodes.push_back({std::move(row), left, right, 1 + Size(left) + Size(right), 1 + std::max(Height(left), Height(right))}); return &nodes.back();
    }
    const NativeListNode* Balance(NativeRowHandle row, const NativeListNode* left, const NativeListNode* right) {
        if (Height(left) > Height(right) + 1) {
            if (Height(left->left) >= Height(left->right)) return Make(left->row, left->left, Make(row, left->right, right));
            const auto* middle = left->right; return Make(middle->row, Make(left->row, left->left, middle->left), Make(row, middle->right, right));
        }
        if (Height(right) > Height(left) + 1) {
            if (Height(right->right) >= Height(right->left)) return Make(right->row, Make(row, left, right->left), right->right);
            const auto* middle = right->left; return Make(middle->row, Make(row, left, middle->left), Make(right->row, middle->right, right->right));
        }
        return Make(std::move(row), left, right);
    }
    const NativeListNode* At(const NativeListNode* root, int row) const {
        while (root) { const int left = Size(root->left); if (row == left) return root; if (row < left) root = root->left; else { row -= left + 1; root = root->right; } } return nullptr;
    }
    struct Ancestor { const NativeListNode* node; bool left; };
    const NativeListNode* RebuildPath(const std::vector<Ancestor>& path, const NativeListNode* result, bool rebalance = true) {
        for (auto it = path.rbegin(); it != path.rend(); ++it) {
            const auto* node = it->node;
            const auto* left = it->left ? result : node->left;
            const auto* right = it->left ? node->right : result;
            result = rebalance ? Balance(node->row, left, right) : Make(node->row, left, right);
        }
        return result;
    }
    const NativeListNode* Insert(const NativeListNode* root, int position, NativeRowHandle row) {
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
    const NativeListNode* Remove(const NativeListNode* root, int position) {
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
        const NativeListNode* result;
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
    const NativeListNode* Replace(const NativeListNode* root, int position, NativeRowHandle row) {
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
    const NativeListNode* Clone(const NativeListNode* old) {
        struct Work { const NativeListNode* node; bool expanded; };
        std::vector<Work> work;
        work.reserve(2 * Height(old) + 1);
        work.push_back({old, false});
        std::vector<const NativeListNode*> built;
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
    void Record(Kind kind, int from, int to, QString key, NativeRowHandle row, const NativeListNode* root) {
        versions.push_back({root}); steps.push_back({kind, from, to, std::move(key), std::move(row), versions.size() - 1});
    }
    void Build(NativeVersionHandle previous, std::vector<NativeRowHandle> rows, quint64 revision) {
        base = previous; empty_base = !previous; epoch = revision; target = std::move(rows);
        if (target.size() > size_t(std::numeric_limits<int>::max())) throw std::length_error("Native inventory exceeds Qt row capacity");
        auto* root = Clone(previous ? previous->root : nullptr);
        IndexedSequence<QString> order; std::vector<NativeRowHandle> old; old.reserve(Size(root)); Collect(root, old);
        for (const auto& row : old) order.append(row->key);
        std::set<QString> keys; for (const auto& row : target) if (!keys.insert(row->key).second) throw std::runtime_error("Duplicate native row identity");
        for (int row = order.size() - 1; row >= 0; --row) if (!keys.contains(order[row])) {
            const auto key = order.takeAt(row); root = Remove(root, row); Record(Kind::Remove, row, -1, key, {}, root);
        }
        for (int row = 0; row < int(target.size()); ++row) {
            const auto& next = target[row]; const int previous_row = order.indexOf(next->key);
            if (previous_row < 0) {
                if (order.size() == std::numeric_limits<int>::max()) throw std::length_error("Native transition exceeds Qt row capacity");
                order.insertAt(row, next->key); root = Insert(root, row, next); Record(Kind::Insert, row, -1, next->key, next, root);
            } else {
                if (previous_row != row) {
                    const auto value = At(root, previous_row)->row; root = Remove(root, previous_row); root = Insert(root, row, value);
                    order.insertAt(row, order.takeAt(previous_row)); Record(Kind::Move, previous_row, row, next->key, {}, root);
                }
                if (At(root, row)->row != next) { root = Replace(root, row, next); Record(Kind::Change, row, -1, next->key, next, root); }
            }
        }
        versions.push_back({root});
    }
};
NativeListModel::NativeListModel(presentation::Post post, QObject* parent, Queues queues) : QAbstractProxyModel(parent)
{
    if (post || queues.core || queues.utility || queues.retire || queues.hold) configureServices(std::move(post), std::move(queues));
}
void NativeListModel::configureServices(presentation::Post post, Queues queues)
{
    Q_ASSERT(!m_input);
    if (bool(queues.core) != bool(queues.utility)) throw std::invalid_argument{"Both list queues must be supplied"};
    if ((queues.retire || queues.hold) && !queues.core) throw std::invalid_argument{"List retirement requires the list queues"};
    m_post = std::move(post);
    m_queues = std::move(queues);
    if (!m_queues.retire && !m_queues.hold) {
        m_owned_executor = std::make_shared<execution::Executor>(execution::Executor::Config{});
        if (!m_queues.core) {
            m_owns_dispatch = true;
            m_queues.core = std::make_shared<execution::SerialQueue>(m_owned_executor->makeQueue(execution::Lane::Core));
            m_queues.utility = std::make_shared<execution::SerialQueue>(m_owned_executor->makeQueue(execution::Lane::Utility));
        }
        m_owned_cleanup = std::make_shared<execution::ValueCleanupQueue>(
            std::make_shared<execution::SerialQueue>(m_owned_executor->makeQueue(execution::Lane::Utility)));
        m_queues.hold = [cleanup = m_owned_cleanup](auto retire) { return cleanup->hold(std::move(retire)); };
    }
    if (!m_post) {
        m_bridge = presentation::MakeGuiObject();
        m_post = [bridge = m_bridge](auto turn) { QMetaObject::invokeMethod(bridge.get(), std::move(turn), Qt::QueuedConnection); };
    }
    m_post = presentation::ReliablePost(std::move(m_post));
    m_input = m_queues.retain<Input>([] { return std::make_unique<Input>(); });
    m_view = m_queues.retain<View>([] { return std::make_unique<View>(); });
    if (!m_input || !m_view) m_error = tr("The list is not available.");
}
NativeListModel::~NativeListModel()
{
    close();
    finishClose();
    if (!m_owned_executor) return;

    auto join = JoinThreadAfterDrain([executor = std::move(m_owned_executor)] { executor->stopAndJoin(); },
        QCoreApplication::instance(), [] {});
    if (!m_owns_dispatch) {
        m_owned_cleanup->close(std::move(join));
        return;
    }
    m_queues.core->close();
    m_queues.core->whenDrained([utility = m_queues.utility, cleanup = m_owned_cleanup, join = std::move(join)] {
        utility->close();
        utility->whenDrained([cleanup, join] { cleanup->close(join); });
    });
}
void NativeListModel::close()
{
    m_stopping = true; m_pending = false; ++m_generation;
    if (m_query) m_query->close();
    if (!m_notifying) finishClose();
}
void NativeListModel::finishClose()
{
    m_query.reset(); m_rebase.reset(); m_plan.reset(); m_deferred.reset();
    m_version.reset(); m_completed.reset(); if (m_input) { std::lock_guard lock{m_input->mutex}; m_input->published.reset(); }
    m_view.reset(); m_input.reset(); m_read = {};
}
int NativeListModel::rowCount(const QModelIndex& parent) const { return parent.isValid() || !m_view ? 0 : m_view->order.size(); }
QModelIndex NativeListModel::index(int row, int column, const QModelIndex& parent) const { return parent.isValid() || column != 0 || row < 0 || row >= rowCount() ? QModelIndex{} : createIndex(row, column, m_view->order[row]); }
QVariant NativeListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.model() != this || index.row() < 0 || index.row() >= rowCount()) return {};
    return static_cast<const View::Slot*>(index.internalPointer())->row->values.value(role);
}
NativeRowHandle NativeListModel::rowHandle(const QModelIndex& index) const {
    if (!index.isValid() || index.model() != this || index.row() < 0 || index.row() >= rowCount()) return {};
    return static_cast<const View::Slot*>(index.internalPointer())->row;
}
NativeRowHandle NativeListModel::rowForKey(const QString& key) const { if (!m_view) return {}; const auto found = m_view->slots.find(key); return found == m_view->slots.end() ? NativeRowHandle{} : found->second.row; }
int NativeListModel::indexForKey(const QString& key) const { if (!m_view) return -1; const auto found = m_view->slots.find(key); return found == m_view->slots.end() ? -1 : m_view->order.indexOf(const_cast<View::Slot*>(&found->second)); }
std::vector<NativeRowHandle> NativeListModel::Rows(const NativeVersionHandle& version) { std::vector<NativeRowHandle> rows; if (version) { rows.reserve(Size(version->root)); Collect(version->root, rows); } return rows; }
void NativeListModel::setRead(Read read, execution::Lane lane) { m_lane = lane; Q_ASSERT(!m_query); if (!m_input) configureServices({}); m_read = std::move(read); }
execution::SnapshotQuery<NativeListModel::PlanHandle>::Post NativeListModel::postNative(execution::Lane lane) const
{
    const bool on_utility = lane == execution::Lane::Domain;
    auto worker = on_utility ? m_queues.utility : m_queues.core;
    return [worker = std::move(worker), utility = m_queues.utility, on_utility](std::shared_ptr<execution::Task> packet) {
        const bool accepted = worker->post([utility, on_utility, packet = std::move(packet)] {
            packet->execute();
            const auto deliver = [packet] { packet->deliver(); };
            if (on_utility || !utility->post(deliver)) deliver();
        });
        return execution::SubmitResult{{}, accepted ? execution::Rejection::None : execution::Rejection::Transport};
    };
}
void NativeListModel::setError(const QString& error) { if (m_error == error) return; m_error = error; Q_EMIT readErrorChanged(); }
void NativeListModel::invalidateProjection() { if (m_stopping || !m_input) return; { std::lock_guard lock{m_input->mutex}; ++m_input->epoch; } m_plan.reset(); ++m_generation; m_rebase.reset(); }
void NativeListModel::refreshNative()
{
    if (m_stopping || !m_input || !m_view || !m_read) return;
    if (!m_query) {
        const QPointer<NativeListModel> guard{this};
        m_query = std::make_shared<execution::SnapshotQuery<PlanHandle>>([input = m_input, read = m_read, queues = m_queues] {
            NativeVersionHandle base; quint64 epoch; { std::lock_guard lock{input->mutex}; base = input->published; epoch = input->epoch; }
            auto rows = read(); auto plan = queues.retain<Plan>([&] { auto value = std::make_unique<Plan>(); value->Build(base, std::move(rows), epoch); return value; });
            if (!plan) throw std::runtime_error("Native projection is unavailable");
            return PlanHandle{std::move(plan)};
        }, [guard, post = m_post] { post([guard] { if (guard) guard->publishQuery(); }); }, postNative(m_lane));
    }
    m_pending = true; m_query->request(); Q_EMIT projectionChanged();
}
void NativeListModel::publishQuery()
{
    if (m_stopping || !m_query) return;
    const auto result = m_query->take(); if (!result) return;
    if (!result->snapshot || !*result->snapshot) { m_pending = bool(m_plan); const QPointer<NativeListModel> guard{this}; setError(tr("The list could not be refreshed. Try again.")); if (guard && !m_stopping) Q_EMIT projectionChanged(); return; }
    acceptPlan(*result->snapshot);
}
void NativeListModel::acceptPlan(PlanHandle plan)
{
    if (stagePlan(std::move(plan))) applyTurn(m_generation);
}
bool NativeListModel::stagePlan(PlanHandle plan)
{
    if (m_stopping || !m_input) return false;
    { std::lock_guard lock{m_input->mutex}; if (plan->epoch != m_input->epoch) return false; }
    if (m_notifying || m_applying) { m_deferred = std::move(plan); return false; }
    m_plan.reset(); ++m_generation;
    const auto base = plan->base.lock();
    if ((!plan->empty_base && base != m_version) || (plan->empty_base && m_version)) { rebase(std::move(plan)); return false; }
    m_plan = std::move(plan); m_step = 0; m_pending = true;
    const auto generation = m_generation;
    const QPointer<NativeListModel> guard{this};
    setError({});
    return guard && !m_stopping && generation == m_generation && bool(m_plan);
}
void NativeListModel::rebase(PlanHandle target)
{
    const QPointer<NativeListModel> guard{this}; const auto generation = m_generation; auto weak = std::make_shared<std::weak_ptr<execution::OwnedCommand<PlanHandle>>>();
    auto command = std::make_shared<execution::OwnedCommand<PlanHandle>>([guard, generation, weak, post = m_post] {
        post([guard, generation, weak] {
            if (!guard || guard->m_stopping || generation != guard->m_generation) return;
            const auto command = weak->lock(); if (!command) return; const auto result = command->result(); if (!result) return;
            if (result->value) guard->acceptPlan(*result->value); else { guard->m_pending = false; guard->setError(tr("The view could not be updated. Try again.")); if (guard && !guard->m_stopping) Q_EMIT guard->projectionChanged(); }
        });
    }); *weak = command; m_rebase = command;
    const auto base = m_version;
    if (!startCommand(*command, [queues = m_queues, base, target = std::move(target)] {
        auto plan = queues.retain<Plan>([&] { auto value = std::make_unique<Plan>(); value->Build(base, target->target, target->epoch); return value; });
        if (!plan) throw std::runtime_error("Native projection is unavailable");
        return PlanHandle{std::move(plan)};
    }, execution::Lane::Domain)) { m_pending = false; setError(tr("The view could not be updated. Try again.")); }
}
void NativeListModel::applyTurn(quint64 generation)
{
    if (m_stopping || m_applying || generation != m_generation || !m_plan || !m_view) return;
    const QPointer<NativeListModel> guard{this}; const auto plan = m_plan; const auto view = m_view;
    m_applying = true;
    const auto finish = qScopeGuard([guard] {
        if (!guard) return;
        guard->m_applying = false;
        if (guard->m_stopping || !guard->m_deferred) return;
        auto next = std::exchange(guard->m_deferred, {});
        guard->m_post([guard, next = std::move(next)] {
            if (guard) guard->acceptPlan(next);
        });
    });
    for (int budget = 0; budget < 32 && m_step < plan->steps.size(); ++budget) {
        const auto& step = plan->steps[m_step++]; m_notifying = true;
        if (step.kind == Plan::Kind::Insert) {
            beginInsertRows({}, step.from, step.from); if (!guard) return;
            auto& slot = view->slots.try_emplace(step.key, View::Slot{step.key, step.row}).first->second;
            view->order.insertAt(step.from, &slot); endInsertRows();
        } else if (step.kind == Plan::Kind::Remove) {
            beginRemoveRows({}, step.from, step.from); if (!guard) return;
            view->order.takeAt(step.from); view->slots.erase(step.key); endRemoveRows();
        } else if (step.kind == Plan::Kind::Move) {
            beginMoveRows({}, step.from, step.from, {}, step.to > step.from ? step.to + 1 : step.to); if (!guard) return;
            view->order.insertAt(step.to, view->order.takeAt(step.from)); endMoveRows();
        } else { view->slots.at(step.key).row = step.row; Q_EMIT dataChanged(index(step.from), index(step.from)); }
        if (!guard) return;
        m_notifying = false;
        if (m_stopping) { finishClose(); return; }
        m_version = NativeVersionHandle{plan, &plan->versions[step.version]};
        { std::lock_guard lock{m_input->mutex}; m_input->published = m_version; }
        if (step.kind == Plan::Kind::Remove) rowRemoved(step.key);
        else if (step.kind != Plan::Kind::Move) rowUpdated(step.key, step.row);
        if (!guard || m_stopping) return;
        if (m_deferred) return;
        if (generation != m_generation) return;
    }
    if (m_step == plan->steps.size()) {
        m_version = NativeVersionHandle{plan, &plan->versions.back()}; m_completed = m_version;
        { std::lock_guard lock{m_input->mutex}; m_input->published = m_version; }
        m_plan.reset(); m_rebase.reset(); m_pending = false; Q_EMIT snapshotReady(); if (guard && !m_stopping) Q_EMIT projectionChanged();
    } else m_post([guard, generation] { if (guard) guard->applyTurn(generation); });
}

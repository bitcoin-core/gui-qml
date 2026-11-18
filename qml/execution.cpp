// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/execution.h>

#include <cassert>
#include <limits>
#include <algorithm>
#include <atomic>
#include <stdexcept>

namespace execution {
namespace {
// Accepted ownership retains these handoffs. A throwing port has the same
// terminal contract as a false return; callers keep their posted assertion and
// unconditional termination fallback when assertions are disabled.
template <typename Post>
bool ReservedPost(Post&& post) noexcept
{
    try { return post(); } catch (...) { return false; }
}
}
class TaskQueue::ScopedScheduler final : public Scheduler {
    const std::shared_ptr<TaskQueue> m_queue;
    const uint64_t m_scope;
    bool m_registered{false};
    std::atomic_bool m_closed{false};
public:
    ScopedScheduler(std::shared_ptr<TaskQueue> queue, uint64_t scope) : m_queue(std::move(queue)), m_scope(scope) {}
    ~ScopedScheduler() override { if (m_registered) m_queue->releaseScope(m_scope); }
    void registered() { m_registered = true; }
    SubmitResult submit(std::shared_ptr<Task> task, TaskSpec spec) override {
        if (m_closed) return {{}, Rejection::Closed};
        if (spec.lane != Lane::Core && spec.lane != Lane::Domain) return {{}, Rejection::Invalid};
        return m_queue->admit(std::move(task), spec, false, m_scope);
    }
    SubmitResult reserveCleanup(std::shared_ptr<Task> task, TaskSpec spec) override { return m_queue->admit(std::move(task), spec, true, m_scope); }
    bool activateCleanup(TaskId id) override { return m_queue->activateCleanupForScope(id, m_scope); }
    Borrow borrow(ResourceKey key) override { return m_closed ? Borrow{{}, Rejection::Closed} : m_queue->borrowForScope(key, m_scope); }
    ResourceKey allocateKey() override { return m_queue->allocateKey(); }
    size_t pending() const override { return m_queue->pending(); }
    void stopAcceptingWork() override { m_closed = true; }
    bool cancelQueued(TaskId id) override { return m_queue->cancelQueuedForScope(id, m_scope); }
};

std::shared_ptr<Scheduler> TaskQueue::openActionScope()
{
    std::lock_guard lock{m_mutex};
    if (m_closed || m_ordinary_closed || m_next_scope == std::numeric_limits<uint64_t>::max()) return {};
    const auto id = ++m_next_scope;
    // Construct before recording admission so allocation failure leaves no
    // pending scope that could strand the Native drain.
    auto result = std::make_shared<ScopedScheduler>(shared_from_this(), id);
    m_scopes.insert(id);
    result->registered();
    return result;
}

void TaskQueue::releaseScope(uint64_t scope)
{
    std::lock_guard lock{m_mutex};
    const auto found = m_scopes.find(scope);
    assert(found != m_scopes.end());
    m_scopes.erase(found);
    const auto group = m_scope_idle_cleanups.find(scope);
    if (group != m_scope_idle_cleanups.end() && !group->second.idle.empty()) {
        m_released_cleanup_scopes.insert(std::move(group->second.released_ticket));
    }
    pumpCleanups();
    // An idle original capability may be the final ledger entry. Without a
    // transport turn, workers that already observed it during shutdown would
    // never recheck quiescence. This wake owns no task, payload or effect.
    if (m_closed && m_tasks.empty() && m_total_borrowers == 0 && m_scopes.empty()) {
        const bool posted = ReservedPost([&] {
            return m_routed_dispatch
                ? m_routed_dispatch(Lane::Control, Phase::OutcomeReady, {}, [] {})
                : m_dispatch(Phase::OutcomeReady, {}, [] {});
        });
        assert(posted); // The original capability retained its drain transport.
        if (!posted) std::terminate();
    }
    // Every remaining task, cleanup reservation and borrower has its own
    // ledger entry; cached values do not consume an action-capability slot.
}

ResourceLease::ResourceLease(ResourceLease&& other) noexcept : m_queue(std::move(other.m_queue)), m_key(other.m_key) {}
ResourceLease::~ResourceLease() { if (m_queue) m_queue->releaseBorrower(m_key); }

TaskQueue::TaskQueue(RoutedDispatch dispatch, Options options, size_t core_slots)
    : m_routed_dispatch(std::move(dispatch)), m_options(options), m_core_slots(core_slots)
{
    if (!m_routed_dispatch || core_slots == 0 || options.actions_before_observation == 0) throw std::invalid_argument("Invalid execution transport or core slots");
}

bool TaskQueue::validLane(Lane lane)
{
    switch (lane) {
    case Lane::Domain:
    case Lane::Core:
    case Lane::Control:
    case Lane::Cleanup: return true;
    case Lane::Priority:
    case Lane::Rpc:
    case Lane::Utility: return false;
    }
    return false;
}

void TaskQueue::releaseWorkKey(Record& record)
{
    if (!std::exchange(record.resource_counted, false)) return;
    const auto key = record.spec.key;
    const auto count = m_work_by_key.find(key);
    assert(count != m_work_by_key.end() && count->second);
    if (--count->second == 0) m_work_by_key.erase(count);
}

void TaskQueue::erase(std::map<TaskId, Record>::iterator record)
{
    const auto key = record->second.spec.key;
    if (record->second.cleanup_reservation) {
        m_cleanup_by_key.erase(key);
        unindexCleanup(record->first, record->second.scope);
        if (record->second.scope) {
            const auto group = m_scope_idle_cleanups.find(record->second.scope);
            assert(group != m_scope_idle_cleanups.end() && group->second.reservations);
            if (--group->second.reservations == 0) m_scope_idle_cleanups.erase(group);
        }
    } else {
        releaseWorkKey(record->second);
    }
    m_tasks.erase(record);
    maybeActivateCleanup(key);
}

SubmitResult TaskQueue::submit(std::shared_ptr<Task> task, TaskSpec spec)
{
    return admit(std::move(task), spec, false);
}

SubmitResult TaskQueue::reserveCleanup(std::shared_ptr<Task> task, TaskSpec spec)
{
    return admit(std::move(task), spec, true);
}

SubmitResult TaskQueue::admit(std::shared_ptr<Task> task, TaskSpec spec, bool reserve_cleanup, uint64_t scope)
{
    std::lock_guard lock{m_mutex};
    const bool admitted_action = scope && m_scopes.contains(scope);
    if ((scope && !admitted_action) || (m_closed && !admitted_action)) return {{}, Rejection::Closed};
    if (!task || !validLane(spec.lane)) return {{}, Rejection::Invalid};
    if (reserve_cleanup && spec.lane != Lane::Cleanup) return {{}, Rejection::Invalid};
    const auto cleanup = m_cleanup_by_key.find(spec.key);
    if (reserve_cleanup && cleanup != m_cleanup_by_key.end()) return {{}, Rejection::Invalid};
    if (cleanup != m_cleanup_by_key.end() && m_tasks.at(cleanup->second).cleanup_requested) return {{}, Rejection::Closed};
    if (spec.work_class != WorkClass::Action && spec.work_class != WorkClass::Observation) return {{}, Rejection::Invalid};
    const bool ordinary = spec.lane == Lane::Core || spec.lane == Lane::Domain;
    if (spec.work_class == WorkClass::Observation && !ordinary) return {{}, Rejection::Invalid};
    if (m_ordinary_closed && ordinary && !admitted_action) return {{}, Rejection::Closed};
    // Task identities never wrap into a live record.
    if (m_next_id == std::numeric_limits<uint64_t>::max()) return {{}, Rejection::Closed};
    const TaskId id{++m_next_id};
    bool record_staged{false}, work_created{false}, waiting_pushed{false};
    bool cleanup_indexed{false}, group_created{false}, committed{false};
    // Only this admission's provisional nodes are removed. In particular,
    // rollback never calls erase(), which can pump other accepted ownership.
    const auto rollback = [&]() noexcept {
        if (waiting_pushed) {
            const auto waiting = m_waiting.find(spec.key);
            if (waiting != m_waiting.end()) {
                std::erase(waiting->second, id);
                if (waiting->second.empty() && !coreRunning(spec.key)) {
                    std::erase(m_ready, spec.key);
                    m_waiting.erase(waiting);
                }
            }
        }
        if (cleanup_indexed) m_cleanup_by_key.erase(spec.key);
        if (group_created) m_scope_idle_cleanups.erase(scope);
        if (work_created) {
            const auto work = m_work_by_key.find(spec.key);
            assert(work != m_work_by_key.end() && work->second == 0);
            m_work_by_key.erase(work);
        }
        if (record_staged) m_tasks.erase(id);
    };
    struct Rollback {
        const decltype(rollback)& run;
        const bool& committed;
        ~Rollback() { if (!committed) run(); }
    } undo{rollback, committed};
    auto& record = m_tasks.emplace(id, Record{reserve_cleanup ? Phase::Reserved : Phase::Queued, task, spec, false, reserve_cleanup}).first->second;
    record_staged = true;
    record.scope = scope;
    if (reserve_cleanup) {
        m_cleanup_by_key.emplace(spec.key, id);
        cleanup_indexed = true;
        std::set<TaskId> tickets;
        tickets.insert(id); record.idle_ticket = tickets.extract(id);
        tickets.insert(id); record.ready_ticket = tickets.extract(id);
        if (scope) {
            const auto [group, inserted] = m_scope_idle_cleanups.try_emplace(scope);
            group_created = inserted;
            if (inserted) {
                std::set<uint64_t> ticket;
                ticket.insert(scope); group->second.released_ticket = ticket.extract(scope);
            }
        }
        // Everything below is a nonallocating ownership commit. Each retained
        // index node is transferred again on block/activation/final release.
        if (scope) {
            auto& group = m_scope_idle_cleanups.at(scope);
            ++group.reservations;
            if (!m_borrowers.contains(spec.key) && !m_work_by_key.contains(spec.key)) group.idle.insert(std::move(record.idle_ticket));
        } else if (!m_borrowers.contains(spec.key) && !m_work_by_key.contains(spec.key)) {
            m_idle_cleanups.insert(std::move(record.idle_ticket));
        }
        committed = true;
        pumpCleanups();
        return {id, Rejection::None};
    }
    const auto [work, inserted] = m_work_by_key.try_emplace(spec.key, 0);
    work_created = inserted;
    if (m_routed_dispatch && spec.lane == Lane::Core) {
        auto& waiting = m_waiting[spec.key];
        // Record the inserted ID before adding a new key's list ticket, so a
        // failed ticket allocation can remove the now-empty waiting bucket.
        try { waiting.push_back(id); } catch (...) {
            if (waiting.empty() && !coreRunning(spec.key)) m_waiting.erase(spec.key);
            throw;
        }
        waiting_pushed = true;
        if (waiting.size() == 1 && !coreRunning(spec.key)) m_ready.push_back(spec.key);
        if (!pumpCore(id)) return {{}, Rejection::Transport};
    } else {
        // Constructing std::function can allocate. A rejected/throwing initial
        // port sees no committed task, and its stray callback finds no record.
        std::function<void()> turn = [self = shared_from_this(), id] { self->execute(id); };
        const bool posted = m_routed_dispatch
            ? m_routed_dispatch(spec.lane, Phase::Queued, id, std::move(turn))
            : m_dispatch(Phase::Queued, id, std::move(turn));
        if (!posted) return {{}, Rejection::Transport};
        record.transport_pending = true;
    }
    ++work->second;
    record.resource_counted = true;
    if (cleanup != m_cleanup_by_key.end()) unindexCleanup(cleanup->second, m_tasks.at(cleanup->second).scope);
    committed = true;
    return {id, Rejection::None};
}

void TaskQueue::activateCleanupLocked(TaskId id)
{
    auto& record = m_tasks.at(id);
    const bool posted = ReservedPost([&] {
        std::function<void()> turn = [self = shared_from_this(), id] { self->execute(id); };
        return m_routed_dispatch
            ? m_routed_dispatch(Lane::Cleanup, Phase::Queued, id, std::move(turn))
            : m_dispatch(Phase::Queued, id, std::move(turn));
    });
    // This transport was reserved before resources could be acquired, and is
    // retained until its outcome is consumed. It cannot reject during drain.
    assert(posted);
    if (!posted) std::terminate();
    unindexCleanup(id, record.scope);
    record.phase = Phase::Queued;
    record.cleanup_requested = true; // Physical retirement fences every later use of this resource key.
    record.transport_pending = true;
}

bool TaskQueue::activateCleanup(TaskId id) { return activateCleanupForScope(id, 0); }

bool TaskQueue::activateCleanupForScope(TaskId id, uint64_t scope)
{
    std::lock_guard lock{m_mutex};
    const auto record = m_tasks.find(id);
    if ((scope && (!m_scopes.contains(scope) || record == m_tasks.end() || record->second.scope != scope)) ||
        record == m_tasks.end() || !record->second.cleanup_reservation || record->second.phase != Phase::Reserved || record->second.cleanup_requested ||
        (m_cleanup_all_requested && (!record->second.scope || !m_scopes.contains(record->second.scope)))) return false;
    record->second.cleanup_requested = true;
    maybeActivateCleanup(record->second.spec.key);
    return true;
}

void TaskQueue::maybeActivateCleanup(ResourceKey key)
{
    const auto cleanup = m_cleanup_by_key.find(key);
    if (cleanup != m_cleanup_by_key.end()) {
        const auto id = cleanup->second;
        auto& record = m_tasks.at(id);
        unindexCleanup(id, record.scope);
        if (record.phase == Phase::Reserved && !m_borrowers.contains(key) && !m_work_by_key.contains(key)) {
            if (record.scope && !record.cleanup_requested) {
                auto& group = m_scope_idle_cleanups.at(record.scope);
                group.idle.insert(std::move(record.idle_ticket));
                if (!m_scopes.contains(record.scope)) m_released_cleanup_scopes.insert(std::move(group.released_ticket));
            } else {
                m_idle_cleanups.insert(std::move(record.idle_ticket));
                if (record.cleanup_requested) m_ready_cleanups.insert(std::move(record.ready_ticket));
            }
        }
    }
    pumpCleanups();
}

void TaskQueue::unindexCleanup(TaskId id, uint64_t scope)
{
    auto& record = m_tasks.at(id);
    if (const auto idle = m_idle_cleanups.find(id); idle != m_idle_cleanups.end()) record.idle_ticket = m_idle_cleanups.extract(idle);
    if (const auto ready = m_ready_cleanups.find(id); ready != m_ready_cleanups.end()) record.ready_ticket = m_ready_cleanups.extract(ready);
    const auto group = m_scope_idle_cleanups.find(scope);
    if (group != m_scope_idle_cleanups.end()) {
        if (const auto idle = group->second.idle.find(id); idle != group->second.idle.end()) record.idle_ticket = group->second.idle.extract(idle);
        if (group->second.idle.empty()) {
            if (const auto released = m_released_cleanup_scopes.find(scope); released != m_released_cleanup_scopes.end()) {
                group->second.released_ticket = m_released_cleanup_scopes.extract(released);
            }
        }
    }
}

void TaskQueue::pumpCleanups()
{
    // Dispatch a bounded batch per ingress turn. Outcomes continue the
    // indexed ready set.
    for (size_t dispatched = 0; dispatched < 16; ++dispatched) {
        const auto& ready = m_cleanup_all_requested ? m_idle_cleanups : m_ready_cleanups;
        if (ready.empty() && m_released_cleanup_scopes.empty()) return;
        const auto id = !ready.empty() ? *ready.begin() : *m_scope_idle_cleanups.at(*m_released_cleanup_scopes.begin()).idle.begin();
        activateCleanupLocked(id);
    }
}

Borrow TaskQueue::borrow(ResourceKey key)
{
    return borrowForScope(key, 0);
}

Borrow TaskQueue::borrowForScope(ResourceKey key, uint64_t scope)
{
    std::lock_guard lock{m_mutex};
    const bool admitted_action = scope && m_scopes.contains(scope);
    if ((scope && !admitted_action) || ((m_closed || m_ordinary_closed) && !admitted_action)) return {{}, Rejection::Closed};
    const auto cleanup = m_cleanup_by_key.find(key);
    if (cleanup == m_cleanup_by_key.end()) return {{}, Rejection::Invalid};
    if (scope && m_tasks.at(cleanup->second).scope != scope) return {{}, Rejection::Invalid};
    if (m_tasks.at(cleanup->second).cleanup_requested || m_tasks.at(cleanup->second).phase != Phase::Reserved) return {{}, Rejection::Closed};
    ++m_borrowers[key];
    ++m_total_borrowers;
    unindexCleanup(cleanup->second, m_tasks.at(cleanup->second).scope);
    return {ResourceLease{shared_from_this(), key}, Rejection::None};
}

void TaskQueue::releaseBorrower(ResourceKey key)
{
    std::lock_guard lock{m_mutex};
    if (--m_borrowers.at(key) == 0) m_borrowers.erase(key);
    --m_total_borrowers;
    maybeActivateCleanup(key);
}

ResourceKey TaskQueue::allocateKey()
{
    std::lock_guard lock{m_mutex};
    if (m_next_owner == 0) throw std::overflow_error("Execution resource identity exhausted");
    const auto owner = m_next_owner;
    m_next_owner = owner == std::numeric_limits<uint64_t>::max() ? 0 : owner + 1;
    return {owner, 1};
}

bool TaskQueue::coreRunning(ResourceKey key) const noexcept
{
    return std::find(m_running.begin(), m_running.end(), key) != m_running.end();
}

void TaskQueue::releaseCore(ResourceKey key) noexcept
{
    const auto running = std::find(m_running.begin(), m_running.end(), key);
    assert(running != m_running.end());
    const auto waiting = m_waiting.find(key);
    assert(waiting != m_waiting.end());
    if (!waiting->second.empty()) m_ready.splice(m_ready.end(), m_running, running);
    else { m_running.erase(running); m_waiting.erase(waiting); }
}

bool TaskQueue::pumpCore(std::optional<TaskId> proposed)
{
    while (m_running.size() < m_core_slots && !m_ready.empty()) {
        const auto wanted = m_action_burst < m_options.actions_before_observation ? WorkClass::Action : WorkClass::Observation;
        const auto preferred = std::find_if(m_ready.begin(), m_ready.end(), [&](ResourceKey key) {
            return m_tasks.at(m_waiting.at(key).front()).spec.work_class == wanted;
        });
        const auto selected = preferred == m_ready.end() ? m_ready.begin() : preferred;
        const auto key = *selected;
        auto& waiting = m_waiting.at(key);
        const auto id = waiting.front();
        bool posted{false};
        try {
            std::function<void()> turn = [self = shared_from_this(), id] { self->execute(id); };
            posted = m_routed_dispatch(Lane::Core, Phase::Queued, id, std::move(turn));
        } catch (...) {
            // A proposed admission can still roll back. Older tasks already
            // own their outcome and use the same notStarted path as rejection.
            if (proposed == id) throw;
        }
        if (!posted && proposed == id) return false;
        // This per-selection commit neither allocates nor restores whole
        // inventories. Older successfully posted work remains independently owned.
        m_running.splice(m_running.end(), m_ready, selected);
        waiting.pop_front();
        if (m_tasks.at(id).spec.work_class == WorkClass::Observation) m_action_burst = 0;
        else if (m_action_burst < m_options.actions_before_observation) ++m_action_burst;
        m_tasks.at(id).transport_pending = posted;
        if (!posted) {
            releaseCore(key);
            queueOutcome(id, NotStarted::Cause::Transport);
        }
    }
    return true;
}

void TaskQueue::queueOutcome(TaskId id, std::optional<NotStarted::Cause> not_started)
{
    auto& record = m_tasks.at(id);
    record.phase = Phase::OutcomeReady;
    if (not_started) {
        // An admitted task whose transport later rejects still owns its failure
        // and captures. Reserved cleanup disposes them before domain delivery.
        const bool posted = ReservedPost([&] {
            return m_routed_dispatch(Lane::Cleanup, Phase::OutcomeReady, id,
                [self = shared_from_this(), id, task = record.task, cause = *not_started] {
                    task->notStarted(cause);
                    std::lock_guard lock{self->m_mutex};
                    self->queueOutcome(id);
                });
        });
        assert(posted);
        if (!posted) std::terminate();
        return;
    }
    const bool posted = ReservedPost([&] {
        std::function<void()> turn = [self = shared_from_this(), id] { self->deliver(id); };
        return m_routed_dispatch
            ? m_routed_dispatch(Lane::Domain, Phase::OutcomeReady, id, std::move(turn))
            : m_dispatch(Phase::OutcomeReady, id, std::move(turn));
    });
    // Accepted outcomes retain their transport until consumption, also during
    // drain. A broken transport is a lifetime bug, never a silent lost result.
    assert(posted);
    if (!posted) std::terminate();
}

void TaskQueue::execute(TaskId id)
{
    std::shared_ptr<Task> task;
    TaskSpec spec;
    bool cancelled;
    {
        std::lock_guard lock{m_mutex};
        const auto found = m_tasks.find(id);
        if (found == m_tasks.end()) return;
        found->second.transport_pending = false;
        // A cancelled turn retains transport capacity until actual dequeue,
        // even when its resource-owning outcome was consumed independently.
        if (found->second.phase != Phase::Queued) {
            if (found->second.outcome_consumed) erase(found);
            return;
        }
        auto& record = found->second;
        record.phase = Phase::Executing;
        task = record.task;
        spec = record.spec;
        cancelled = record.cancelled;
    }
    if (cancelled) task->notStarted(NotStarted::Cause::Cancelled);
    else task->execute();
    {
        std::lock_guard lock{m_mutex};
        if (m_routed_dispatch && spec.lane == Lane::Core) releaseCore(spec.key);
        queueOutcome(id);
        if (m_routed_dispatch) pumpCore();
    }
}

void TaskQueue::deliver(TaskId id)
{
    std::shared_ptr<Task> task;
    {
        std::lock_guard lock{m_mutex};
        auto& record = m_tasks.at(id);
        record.phase = Phase::Delivering;
        task = record.task;
    }
    task->deliver();
    {
        std::lock_guard lock{m_mutex};
        const auto found = m_tasks.find(id);
        if (found->second.transport_pending) {
            found->second.outcome_consumed = true;
            found->second.phase = Phase::AwaitingTransport;
            found->second.task.reset();
            releaseWorkKey(found->second);
            maybeActivateCleanup(found->second.spec.key);
        } else {
            erase(found);
        }
    }
    // The local task (including an unobserved owning result) dies on this lane.
}

void TaskQueue::close()
{
    std::lock_guard lock{m_mutex};
    m_closed = true;
    m_cleanup_all_requested = true;
    pumpCleanups();
}

void TaskQueue::stopAcceptingWork()
{
    std::lock_guard lock{m_mutex};
    m_ordinary_closed = true;
}

bool TaskQueue::cancelQueued(TaskId id) { return cancelQueuedForScope(id, 0); }

bool TaskQueue::cancelQueuedForScope(TaskId id, uint64_t scope)
{
    std::lock_guard lock{m_mutex};
    const auto record = m_tasks.find(id);
    if ((scope && (!m_scopes.contains(scope) || record == m_tasks.end() || record->second.scope != scope)) ||
        record == m_tasks.end() || record->second.phase != Phase::Queued || record->second.cancelled || record->second.cleanup_reservation) return false;
    record->second.cancelled = true;
    if (m_routed_dispatch) {
        if (record->second.spec.lane == Lane::Core) {
            const auto key = record->second.spec.key;
            auto& waiting = m_waiting.at(key);
            const auto queued = std::find(waiting.begin(), waiting.end(), id);
            if (queued != waiting.end()) {
                waiting.erase(queued);
                if (waiting.empty() && !coreRunning(key)) {
                    m_waiting.erase(key);
                    std::erase(m_ready, key);
                }
            } else {
                releaseCore(key);
            }
        }
        queueOutcome(id, NotStarted::Cause::Cancelled);
        pumpCore();
    }
    return true;
}

size_t TaskQueue::pending() const
{
    std::lock_guard lock{m_mutex};
    return m_tasks.size() + m_total_borrowers + m_scopes.size();
}

std::optional<Phase> TaskQueue::phase(TaskId id) const
{
    std::lock_guard lock{m_mutex};
    const auto it = m_tasks.find(id);
    if (it == m_tasks.end()) return std::nullopt;
    return it->second.phase;
}
} // namespace execution

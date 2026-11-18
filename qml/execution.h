// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_EXECUTION_H
#define BITCOIN_QML_EXECUTION_H

#ifndef Q_MOC_RUN
#include <compare>
#endif
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <limits>
#include <list>
#include <mutex>
#include <optional>
#include <set>
#include <type_traits>
#include <utility>
#include <variant>

namespace execution {
struct TaskId {
    uint64_t value{0};
    auto operator<=>(const TaskId&) const = default;
};
enum class Rejection { None, Closed, Transport, Capacity, Invalid };
enum class Lane { Domain, Core, Control, Cleanup, Priority, Rpc, Utility };
enum class WorkClass { Action, Observation };
struct ResourceKey {
    uint64_t owner{0};
    uint64_t resource{0};
    auto operator<=>(const ResourceKey&) const = default;
};
struct TaskSpec {
    Lane lane{Lane::Core};
    ResourceKey key{};
    WorkClass work_class{WorkClass::Action};
};
struct Options {
    // Core dispatch prefers a waiting observation after this many actions.
    size_t actions_before_observation{4};
    uint64_t first_resource_owner{1};
    // Last-issued action identity; inject its boundary for deterministic tests.
    uint64_t initial_scope_sequence{0};
    // Last-issued task identity; admission must never wrap into a live record.
    uint64_t initial_task_sequence{0};
};
struct SubmitResult {
    TaskId id;
    Rejection rejection{Rejection::None};
    explicit operator bool() const { return rejection == Rejection::None; }
};
class TaskQueue;
class ResourceLease
{
public:
    ResourceLease(ResourceLease&& other) noexcept;
    ResourceLease(const ResourceLease&) = delete;
    ResourceLease& operator=(const ResourceLease&) = delete;
    ~ResourceLease();
private:
    friend class TaskQueue;
    ResourceLease(std::shared_ptr<TaskQueue> queue, ResourceKey key) noexcept : m_queue(std::move(queue)), m_key(key) {}
    std::shared_ptr<TaskQueue> m_queue;
    ResourceKey m_key;
};
struct Borrow {
    std::optional<ResourceLease> lease;
    Rejection rejection{Rejection::None};
    explicit operator bool() const { return lease.has_value() && rejection == Rejection::None; }
};
enum class Phase { Reserved, Queued, Executing, OutcomeReady, Delivering, AwaitingTransport };
class NotStarted final : public std::exception
{
public:
    enum class Cause { Cancelled, Transport };
    explicit NotStarted(Cause value) : cause(value) {}
    const char* what() const noexcept override { return "Task did not start"; }
    const Cause cause;
};
/** Why a task did not start, or nothing when the error is absent or something else. */
inline std::optional<NotStarted::Cause> NotStartedCause(const std::exception_ptr& error) noexcept
{
    if (!error) return {};
    try { std::rethrow_exception(error); }
    catch (const NotStarted& stopped) { return stopped.cause; }
    catch (...) { return {}; }
}

/** Owned data and callbacks. Execution and outcome delivery are separate turns. */
struct Task {
    virtual ~Task() = default;
    virtual void execute() = 0;
    virtual void notStarted(NotStarted::Cause cause) = 0;
    virtual void deliver() = 0;
};

class Scheduler
{
public:
    virtual ~Scheduler() = default;
    // Admit an original action before quiet close. Its capability can complete
    // dependent stages after parent admission closes, under all shared budgets.
    // An unsupported or rejected capability returns null, before any effect.
    virtual std::shared_ptr<Scheduler> openActionScope() { return {}; }
    virtual SubmitResult submit(std::shared_ptr<Task> task, TaskSpec spec = {}) = 0;
    virtual SubmitResult reserveCleanup(std::shared_ptr<Task> task, TaskSpec spec) = 0;
    virtual bool activateCleanup(TaskId id) = 0;
    virtual Borrow borrow(ResourceKey key) = 0;
    virtual ResourceKey allocateKey() = 0;
    virtual size_t pending() const = 0;
    virtual void stopAcceptingWork() = 0;
    virtual bool cancelQueued(TaskId id) = 0;
};

/** Value-only command/effect adapter; neither callback may access GUI objects. */
template <typename Work, typename Done>
std::shared_ptr<Task> MakeTask(Work work, Done done, std::function<void(std::exception_ptr)> failed)
{
    using Return = std::invoke_result_t<Work>;
    static_assert(!std::is_reference_v<Return>, "Return an owned result");
    using Result = std::conditional_t<std::is_void_v<Return>, std::monostate, Return>;
    struct Packet final : Task {
        std::optional<Work> work;
        Done done;
        std::function<void(std::exception_ptr)> failed;
        std::optional<Result> result;
        std::exception_ptr error;
        Packet(Work w, Done d, std::function<void(std::exception_ptr)> f)
            : work(std::move(w)), done(std::move(d)), failed(std::move(f)) {}
        void execute() override
        {
            try {
                if constexpr (std::is_void_v<Return>) {
                    std::invoke(*work);
                    result.emplace();
                } else {
                    result.emplace(std::invoke(*work));
                }
            } catch (...) {
                error = std::current_exception();
            }
            work.reset();
        }
        void deliver() override
        {
            if (error) {
                if (failed) failed(error);
            } else if constexpr (std::is_void_v<Return>) {
                std::invoke(done);
            } else {
                std::invoke(done, std::move(*result));
            }
        }
        void notStarted(NotStarted::Cause cause) override
        {
            error = std::make_exception_ptr(NotStarted{cause});
            work.reset();
        }
    };
    return std::make_shared<Packet>(std::move(work), std::move(done), std::move(failed));
}

template <typename Work, typename Done>
SubmitResult Submit(Scheduler& scheduler, Work work, Done done,
                    std::function<void(std::exception_ptr)> failed = {}, TaskSpec spec = {}, bool reserve_cleanup = false)
{
    auto packet = MakeTask(std::move(work), std::move(done), std::move(failed));
    return reserve_cleanup ? scheduler.reserveCleanup(std::move(packet), spec) : scheduler.submit(std::move(packet), spec);
}

/**
 * Thread-safe admission and an owned task ledger. Dispatch must enqueue, never
 * invoke inline. Once admitted, the transport must remain available for its
 * outcome even after admission closes. Routed Core work is serial per resource
 * key; its bounded independent keys may run concurrently. Domain consumption
 * uses a separate serial transport and never depends on a GUI observer.
 */
class TaskQueue final : public Scheduler, public std::enable_shared_from_this<TaskQueue>
{
public:
    using Dispatch = std::function<bool(Phase, TaskId, std::function<void()>)>;
    using RoutedDispatch = std::function<bool(Lane, Phase, TaskId, std::function<void()>)>;
    // Legacy transport ordering remains available until its consumers migrate.
    explicit TaskQueue(Dispatch dispatch) : m_dispatch(std::move(dispatch)) {}
    TaskQueue(Dispatch dispatch, Options options) : m_dispatch(std::move(dispatch)), m_options(options) {}
    TaskQueue(RoutedDispatch dispatch, Options options, size_t core_slots);
    std::shared_ptr<Scheduler> openActionScope() override;
    SubmitResult submit(std::shared_ptr<Task> task, TaskSpec spec = {}) override;
    SubmitResult reserveCleanup(std::shared_ptr<Task> task, TaskSpec spec) override;
    bool activateCleanup(TaskId id) override;
    Borrow borrow(ResourceKey key) override;
    ResourceKey allocateKey() override;
    void stopAcceptingWork() override;
    bool cancelQueued(TaskId id) override;
    void close();
    size_t pending() const override;
    std::optional<Phase> phase(TaskId id) const;

private:
    friend class ResourceLease;
    class ScopedScheduler;
    friend class ScopedScheduler;
    void execute(TaskId id);
    void deliver(TaskId id);
    struct Record {
        Phase phase;
        std::shared_ptr<Task> task;
        TaskSpec spec;
        bool cancelled{false};
        bool cleanup_reservation{false};
        bool cleanup_requested{false};
        bool transport_pending{false};
        bool outcome_consumed{false};
        bool resource_counted{false};
        uint64_t scope{0};
        // Reservation admission allocates these ordered-index nodes once.
        // Blocking, activation and final release only transfer ownership.
        std::set<TaskId>::node_type idle_ticket{};
        std::set<TaskId>::node_type ready_ticket{};
    };
    static bool validLane(Lane lane);
    void erase(std::map<TaskId, Record>::iterator record);
    SubmitResult admit(std::shared_ptr<Task> task, TaskSpec spec, bool reserve_cleanup, uint64_t scope = 0);
    Borrow borrowForScope(ResourceKey key, uint64_t scope);
    void releaseScope(uint64_t scope);
    void unindexCleanup(TaskId id, uint64_t scope);
    bool activateCleanupForScope(TaskId id, uint64_t scope);
    bool cancelQueuedForScope(TaskId id, uint64_t scope);
    void activateCleanupLocked(TaskId id);
    void maybeActivateCleanup(ResourceKey key);
    void pumpCleanups();
    void releaseWorkKey(Record& record);
    void releaseBorrower(ResourceKey key);
    bool pumpCore(std::optional<TaskId> proposed = {});
    void releaseCore(ResourceKey key) noexcept;
    bool coreRunning(ResourceKey key) const noexcept;
    void queueOutcome(TaskId id, std::optional<NotStarted::Cause> not_started = {});
    const Dispatch m_dispatch;
    const RoutedDispatch m_routed_dispatch;
    const Options m_options;
    const size_t m_core_slots{1};
    mutable std::mutex m_mutex;
    std::map<TaskId, Record> m_tasks;
    std::set<uint64_t> m_scopes;
    struct CleanupGroup {
        std::set<TaskId> idle;
        size_t reservations{0};
        std::set<uint64_t>::node_type released_ticket{};
    };
    std::map<uint64_t, CleanupGroup> m_scope_idle_cleanups;
    std::set<uint64_t> m_released_cleanup_scopes;
    uint64_t m_next_scope{m_options.initial_scope_sequence};
    std::map<ResourceKey, size_t> m_work_by_key;
    std::set<TaskId> m_idle_cleanups, m_ready_cleanups;
    bool m_cleanup_all_requested{false};
    std::map<ResourceKey, std::deque<TaskId>> m_waiting;
    // One list node per admitted Core key moves between these inventories.
    // Returning a running key after its effect never allocates.
    std::list<ResourceKey> m_ready, m_running;
    std::map<ResourceKey, TaskId> m_cleanup_by_key;
    std::map<ResourceKey, size_t> m_borrowers;
    size_t m_total_borrowers{0};
    uint64_t m_next_owner{m_options.first_resource_owner};
    size_t m_action_burst{0};
    uint64_t m_next_id{m_options.initial_task_sequence};
    bool m_ordinary_closed{false};
    bool m_closed{false};
};
} // namespace execution

#endif // BITCOIN_QML_EXECUTION_H

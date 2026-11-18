// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/backendexecutor.h>
#include <qml/asyncjoin.h>
#include <qml/executor.h>

#include <QCoreApplication>
#include <QDebug>
#include <QMetaObject>
#include <QScopeGuard>
#include <QStringList>

#include <vector>
#include <atomic>
#include <set>
#include <QThread>

class BackendExecutorWorker : public QObject
{
    Q_OBJECT
public:
    void runTask(quint64 id, std::function<void()> work, bool owns_outcome = false)
    {
        if (owns_outcome) m_owned_outcomes.insert(id);
        m_retained_tasks.emplace(id, std::move(work));
        m_retained_tasks.at(id)();
        Q_EMIT completed(id);
    }
    void guiEntered(quint64 id) { m_active_gui.insert(id); }
    void releaseTask(quint64 id) { m_active_gui.erase(id); m_owned_outcomes.erase(id); m_retained_tasks.erase(id); }
    void releaseAllTasks()
    {
        for (auto it = m_retained_tasks.begin(); it != m_retained_tasks.end();) {
            if (m_active_gui.contains(it->first) || m_owned_outcomes.contains(it->first)) ++it;
            else it = m_retained_tasks.erase(it);
        }
    }
    void beginShutdown(QThread* thread, std::shared_ptr<execution::TaskQueue> scheduler)
    {
        releaseAllTasks();
        m_shutdown_thread = thread;
        m_scheduler = std::move(scheduler);
        maybeQuit();
    }
    void maybeQuit()
    {
        if (m_shutdown_thread && m_scheduler->pending() == 0 && m_active_gui.empty() && m_owned_outcomes.empty()) {
            if (ready_for_group_shutdown) {
                if (!std::exchange(m_reported_ready, true)) ready_for_group_shutdown(m_shutdown_thread);
            } else {
                QMetaObject::invokeMethod(m_shutdown_thread, &QThread::quit, Qt::QueuedConnection);
            }
        }
    }
    std::function<void(QThread*)> ready_for_group_shutdown;
Q_SIGNALS:
    void completed(quint64 id);
private:
    std::map<quint64, std::function<void()>> m_retained_tasks;
    std::set<quint64> m_active_gui;
    std::set<quint64> m_owned_outcomes;
    QThread* m_shutdown_thread{nullptr};
    std::shared_ptr<execution::TaskQueue> m_scheduler;
    bool m_reported_ready{false};
};

/** GUI coordinates only a bounded native thread-stop handoff. Work/outcomes
 * remain background-owned even when BackendExecutor's GUI owner disappears. */
struct BackendExecutionGroup : std::enable_shared_from_this<BackendExecutionGroup> {
    std::vector<std::pair<BackendExecutorWorker*, QThread*>> workers;
    std::atomic_bool stopping{false};
    std::set<QThread*> ready;
    bool quit_issued{false};

    void checkOnGui()
    {
        if (!stopping || quit_issued) return;
        if (ready.size() == workers.size()) {
            quit_issued = true;
            for (const auto& [worker, thread] : workers) thread->quit();
            return;
        }
        // No worker is allowed to quit before all acknowledge readiness. Raw
        // worker addresses are therefore live throughout this GUI handoff.
        for (const auto& [worker, thread] : workers) {
            if (!ready.contains(thread)) QMetaObject::invokeMethod(worker, &BackendExecutorWorker::maybeQuit, Qt::QueuedConnection);
        }
    }
    void requestCheck()
    {
        if (!stopping) return;
        QMetaObject::invokeMethod(QCoreApplication::instance(), [self = shared_from_this()] { self->checkOnGui(); }, Qt::QueuedConnection);
    }
    void workerReady(QThread* thread)
    {
        QMetaObject::invokeMethod(QCoreApplication::instance(), [self = shared_from_this(), thread] {
            self->ready.insert(thread);
            self->checkOnGui();
        }, Qt::QueuedConnection);
    }
};

struct BackendExecutor::ExecutorState {
    enum class Phase { Queued, Running, Finished, Cancelled };
    struct Control {
        std::atomic<Phase> phase{Phase::Queued};
        CancelPolicy policy;
        explicit Control(CancelPolicy policy_in) : policy(policy_in) {}
    };
    struct Pending {
        std::shared_ptr<Control> control;
        std::function<void(std::optional<Cancelled>)> completion;
    };

    BackendExecutorWorker* worker{nullptr};
    size_t threads_remaining{0};
    std::map<TaskId, Pending> pending;
    std::vector<std::pair<QPointer<QObject>, std::function<void()>>> drain_callbacks;
    bool drained{false};

    void complete(TaskId id)
    {
        Q_ASSERT(QThread::currentThread() == QCoreApplication::instance()->thread());
        const auto it = pending.find(id);
        if (it == pending.end()) return;
        auto task = std::move(it->second);
        pending.erase(it);
        const auto cleanup = qScopeGuard([&] {
            task.completion = {};
            QMetaObject::invokeMethod(worker, [worker = worker, id] { worker->releaseTask(id); worker->maybeQuit(); }, Qt::QueuedConnection);
        });
        const bool cancelled = task.control->phase.load() == Phase::Cancelled;
        task.completion(cancelled ? std::make_optional(Cancelled{Cancelled::Reason::Requested}) : std::nullopt);
    }
};

BackendExecutor::BackendExecutor(QObject* parent) : BackendExecutor(std::nullopt, parent, CleanupAffinity::Independent, {}) {}
BackendExecutor::BackendExecutor(execution::Options limits, QObject* parent, CleanupAffinity cleanup_affinity,
                                 std::shared_ptr<execution::SerialQueue> priority)
    : BackendExecutor(std::make_optional(limits), parent, cleanup_affinity, std::move(priority)) {}

BackendExecutor::BackendExecutor(std::optional<execution::Options> limits, QObject* parent, CleanupAffinity cleanup_affinity,
                                 std::shared_ptr<execution::SerialQueue> priority)
    : QObject(parent), m_state(std::make_shared<ExecutorState>()), m_worker(new BackendExecutorWorker), m_thread(new QThread)
{
    Q_ASSERT(QCoreApplication::instance());
    Q_ASSERT(QThread::currentThread() == QCoreApplication::instance()->thread());
    setObjectName(QStringLiteral("backend"));
    std::vector<std::pair<BackendExecutorWorker*, QThread*>> workers{{m_worker, m_thread}};
    if (limits) {
        m_group = std::make_shared<BackendExecutionGroup>();
        // Keep the legacy GUI-completion worker independent of migrated Core
        // reads: legacy operations may wait for transferred handles to retire.
        // Compatibility, Core, Domain, optional Control and Cleanup.
        workers.emplace_back(new BackendExecutorWorker, new QThread); // Core
        workers.emplace_back(new BackendExecutorWorker, new QThread); // Domain
        if (!priority) workers.emplace_back(new BackendExecutorWorker, new QThread);
        if (cleanup_affinity == CleanupAffinity::Independent) workers.emplace_back(new BackendExecutorWorker, new QThread);
        m_group->workers = workers;
        for (const auto& [worker, thread] : workers) {
            worker->ready_for_group_shutdown = [group = m_group](QThread* ready_thread) { group->workerReady(ready_thread); };
        }
        m_scheduler = std::make_shared<execution::TaskQueue>(
            [group = m_group, cleanup_affinity, priority = std::move(priority)](
                execution::Lane lane, execution::Phase, execution::TaskId, std::function<void()> turn) {
                auto run = [group, turn = std::move(turn)] {
                    turn();
                    group->requestCheck();
                };
                if (lane == execution::Lane::Control && priority) return priority->post(std::move(run));
                size_t index{0};
                switch (lane) {
                case execution::Lane::Core: index = 1; break;
                case execution::Lane::Domain: index = 2; break;
                case execution::Lane::Control: index = 3; break;
                case execution::Lane::Cleanup: index = cleanup_affinity == CleanupAffinity::Core ? 1 : group->workers.size() - 1; break;
                case execution::Lane::Priority:
                case execution::Lane::Rpc:
                case execution::Lane::Utility: return false;
                }
                return QMetaObject::invokeMethod(group->workers[index].first, std::move(run), Qt::QueuedConnection);
            }, *limits, 1);
    } else {
        m_scheduler = std::make_shared<execution::TaskQueue>(
            [worker = m_worker](execution::Phase, execution::TaskId, std::function<void()> turn) {
                return QMetaObject::invokeMethod(worker, [worker, turn = std::move(turn)] {
                    turn();
                    worker->maybeQuit();
                }, Qt::QueuedConnection);
            });
    }
    m_state->worker = m_worker;
    m_state->threads_remaining = workers.size();
    for (size_t i{0}; i < workers.size(); ++i) {
        const auto [worker, thread] = workers[i];
        const auto suffix = i == 0 ? QString{} : QStringLiteral("-%1").arg(i);
        thread->setObjectName(objectName() + suffix);
        connect(this, &QObject::objectNameChanged, thread, [thread, suffix](const QString& name) { thread->setObjectName(name + suffix); });
        worker->moveToThread(thread);
        connect(thread, &QThread::finished, worker, &QObject::deleteLater);
#ifdef Q_OS_MACOS
        thread->setStackSize(8 * 1024 * 1024);
#endif
        thread->start();
    }
    connect(m_worker, &BackendExecutorWorker::completed, QCoreApplication::instance(),
            [state = m_state](TaskId id) { state->complete(id); }, Qt::QueuedConnection);
    connect(m_worker, &BackendExecutorWorker::completed, this, [this](quint64 id) {
        const auto it = m_gui_completions.find(id);
        if (it == m_gui_completions.end()) return;
        auto gui_completion = std::move(it->second);
        m_gui_completions.erase(it);
        auto* worker = m_worker;
        // This entry precedes any reentrant shutdown posted by the callback.
        // Keep its backend packet on the worker until the GUI callback releases
        // its own reference, even if that callback destroys the executor facade.
        QMetaObject::invokeMethod(worker, [worker, id] { worker->guiEntered(id); }, Qt::QueuedConnection);
        const auto release = qScopeGuard([worker, id, &gui_completion] {
            gui_completion = {};
            QMetaObject::invokeMethod(worker, [worker, id] { worker->releaseTask(id); worker->maybeQuit(); }, Qt::QueuedConnection);
        });
        gui_completion();
    }, Qt::QueuedConnection);
}

BackendExecutor::~BackendExecutor()
{
    shutdown();
}

bool BackendExecutor::enqueue(std::function<void()> work, std::function<void()> gui_completion)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_stopping) return false;
    const quint64 id{++m_next_task_id};
    m_gui_completions.emplace(id, std::move(gui_completion));
    QMetaObject::invokeMethod(m_worker, [worker = m_worker, id, work = std::move(work)] {
        worker->runTask(id, work);
    }, Qt::QueuedConnection);
    return true;
}

BackendExecutor::TaskId BackendExecutor::enqueueTask(std::function<void(bool)> work, std::function<void(std::optional<Cancelled>)> gui_completion, CancelPolicy policy)
{
    Q_ASSERT(QThread::currentThread() == thread());
    const TaskId id{++m_next_task_id};
    if (m_stopping) {
        work(false);
        QMetaObject::invokeMethod(QCoreApplication::instance(), [completion = std::move(gui_completion)] {
            completion(Cancelled{Cancelled::Reason::Rejected});
        }, Qt::QueuedConnection);
        return id;
    }
    auto control = std::make_shared<ExecutorState::Control>(policy);
    m_state->pending.emplace(id, ExecutorState::Pending{control, std::move(gui_completion)});
    QMetaObject::invokeMethod(m_state->worker, [worker = m_state->worker, id, control, work = std::move(work)] {
        worker->runTask(id, [control, work] {
            auto expected = ExecutorState::Phase::Queued;
            const bool run = control->phase.compare_exchange_strong(expected, ExecutorState::Phase::Running);
            work(run);
            if (run) {
                expected = ExecutorState::Phase::Running;
                control->phase.compare_exchange_strong(expected, ExecutorState::Phase::Finished);
            }
        }, true);
    }, Qt::QueuedConnection);
    return id;
}

bool BackendExecutor::cancel(TaskId id)
{
    Q_ASSERT(QThread::currentThread() == thread());
    const auto it = m_state->pending.find(id);
    if (it == m_state->pending.end()) return false;
    auto& control = *it->second.control;
    auto expected = ExecutorState::Phase::Queued;
    if (control.phase.compare_exchange_strong(expected, ExecutorState::Phase::Cancelled)) {
        QMetaObject::invokeMethod(QCoreApplication::instance(), [state = m_state, id] {
            state->complete(id);
        }, Qt::QueuedConnection);
        return true;
    }
    if (control.policy != CancelPolicy::DiscardRunningResult || expected != ExecutorState::Phase::Running) return false;
    return control.phase.compare_exchange_strong(expected, ExecutorState::Phase::Cancelled);
}

void BackendExecutor::whenDrained(QObject* receiver, std::function<void()> done)
{
    Q_ASSERT(QThread::currentThread() == thread());
    Q_ASSERT(receiver && receiver->thread() == thread());
    if (m_state->drained) {
        QMetaObject::invokeMethod(receiver, std::move(done), Qt::QueuedConnection);
    } else {
        m_state->drain_callbacks.emplace_back(receiver, std::move(done));
    }
}

bool BackendExecutor::isDrained() const
{
    return m_state->drained;
}

void BackendExecutor::shutdown()
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_stopping) return;
    m_stopping = true;
    if (m_group) m_group->stopping = true;
    m_scheduler->close();
    qDebug() << __func__ << ": Stopping thread" << m_thread;
    m_gui_completions.clear();
    disconnect(m_worker, nullptr, this, nullptr);
    const auto stop_worker = [this, started = m_shutdown_started_for_testing](BackendExecutorWorker* worker, QThread* thread) {
        // Register before asking the worker to quit. The test runner can wait
        // for physical joins after releasing all of its local worker owners.
        JoinThreadAsync(thread, QCoreApplication::instance(), [owner = QPointer<BackendExecutor>{this}, state = m_state] {
            if (--state->threads_remaining != 0) return;
            state->drained = true;
            state->worker = nullptr;
            if (owner) {
                owner->m_worker = nullptr;
                owner->m_thread = nullptr;
                Q_EMIT owner->drained();
            }
            auto callbacks = std::move(state->drain_callbacks);
            for (auto& [receiver, done] : callbacks) {
                if (receiver) QMetaObject::invokeMethod(receiver, std::move(done), Qt::QueuedConnection);
            }
        });
        QMetaObject::invokeMethod(worker, [worker, thread, scheduler = m_scheduler, started] {
            worker->beginShutdown(thread, scheduler);
            if (started) started();
        }, Qt::QueuedConnection);
    };
    if (m_group) {
        for (const auto& [worker, thread] : m_group->workers) stop_worker(worker, thread);
    } else {
        stop_worker(m_worker, m_thread);
    }
}

#include <qml/backendexecutor.moc>

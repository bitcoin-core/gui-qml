// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/backendexecutor.h>
#include <qml/asyncjoin.h>

#include <QCoreApplication>
#include <QDebug>
#include <QMetaObject>
#include <QScopeGuard>
#include <QStringList>

#include <vector>
#include <QThread>

namespace {
struct ExecutorRegistry {
    std::map<QThread*, QPointer<BackendExecutor>> owners_by_thread;
    std::vector<std::pair<QPointer<QObject>, std::function<void()>>> drain_callbacks;

    void notifyIfDrained()
    {
        if (!owners_by_thread.empty()) return;
        auto ready_callbacks = std::move(drain_callbacks);
        drain_callbacks.clear();
        for (auto& [receiver, done] : ready_callbacks) {
            if (receiver) WaitForThreadJoins(receiver, std::move(done));
        }
    }
};

ExecutorRegistry& GetExecutorRegistry()
{
    static ExecutorRegistry registry;
    return registry;
}
}

class BackendExecutorWorker : public QObject
{
    Q_OBJECT
public:
    void runTask(quint64 id, std::function<void()> work)
    {
        m_retained_tasks.emplace(id, std::move(work));
        m_retained_tasks.at(id)();
        Q_EMIT completed(id);
    }
    void releaseTask(quint64 id) { m_retained_tasks.erase(id); }
    void releaseAllTasks() { m_retained_tasks.clear(); }
Q_SIGNALS:
    void completed(quint64 id);
private:
    std::map<quint64, std::function<void()>> m_retained_tasks;
};

struct BackendExecutor::ExecutorState {
    BackendExecutorWorker* worker{new BackendExecutorWorker};
    QThread* worker_thread{new QThread};
    std::map<TaskId, std::function<void(std::optional<Cancelled>)>> pending;
    size_t active_callbacks{0};
    bool worker_barrier{false};
    bool finish_scheduled{false};

    void finishShutdownIfReady()
    {
        if (!worker_barrier || !pending.empty() || active_callbacks || finish_scheduled) return;
        finish_scheduled = true;
        QMetaObject::invokeMethod(worker, [worker = worker, thread = worker_thread] {
            worker->releaseAllTasks();
            QMetaObject::invokeMethod(thread, &QThread::quit, Qt::QueuedConnection);
        }, Qt::QueuedConnection);
    }

    void complete(TaskId id)
    {
        Q_ASSERT(QThread::currentThread() == QCoreApplication::instance()->thread());
        const auto it = pending.find(id);
        if (it == pending.end()) return;
        auto completion = std::move(it->second);
        pending.erase(it);
        ++active_callbacks;
        const auto cleanup = qScopeGuard([&] {
            completion = {};
            QMetaObject::invokeMethod(worker, [worker = worker, id] { worker->releaseTask(id); }, Qt::QueuedConnection);
            --active_callbacks;
            finishShutdownIfReady();
        });
        completion(std::nullopt);
    }
};

BackendExecutor::BackendExecutor(QObject* parent)
    : QObject(parent), m_state(std::make_shared<ExecutorState>())
{
    Q_ASSERT(QCoreApplication::instance());
    Q_ASSERT(QThread::currentThread() == QCoreApplication::instance()->thread());
    auto* const worker = m_state->worker;
    auto* const worker_thread = m_state->worker_thread;
    // Keep the name on the thread so cleanup remains identifiable after its owner is destroyed.
    connect(this, &QObject::objectNameChanged, worker_thread, [thread = worker_thread](const QString& name) {
        thread->setObjectName(name);
    });
    setObjectName(QStringLiteral("backend"));
    GetExecutorRegistry().owners_by_thread.emplace(worker_thread, this);
    connect(worker_thread, &QThread::finished, QCoreApplication::instance(), [thread = worker_thread, owner = QPointer<BackendExecutor>{this}, state = m_state] {
        JoinThreadAsync(thread, QCoreApplication::instance(), [thread, owner, state] {
            GetExecutorRegistry().owners_by_thread.erase(thread);
            state->worker = nullptr;
            state->worker_thread = nullptr;
            if (owner) {
                owner->m_drained = true;
                Q_EMIT owner->drained();
            }
            GetExecutorRegistry().notifyIfDrained();
        });
    }, Qt::QueuedConnection);
    worker->moveToThread(worker_thread);
    connect(worker, &BackendExecutorWorker::completed, QCoreApplication::instance(),
            [state = m_state](TaskId id) { state->complete(id); }, Qt::QueuedConnection);
    connect(worker, &BackendExecutorWorker::completed, this, [this](quint64 id) {
        const auto it = m_gui_completions.find(id);
        if (it == m_gui_completions.end()) return;
        auto gui_completion = std::move(it->second);
        m_gui_completions.erase(it);
        const QPointer<BackendExecutor> owner_guard{this};
        gui_completion();
        gui_completion = {};
        if (!owner_guard || m_stopping) return;
        QMetaObject::invokeMethod(m_state->worker, [worker = m_state->worker, id] { worker->releaseTask(id); }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
    connect(worker_thread, &QThread::finished, worker, &QObject::deleteLater);
#ifdef Q_OS_MACOS
    // Core startup can exceed macOS's 512 KiB worker stack, especially under
    // sanitizers (e.g. RandAddStaticEnv). Match the 8 MiB main-thread stack.
    worker_thread->setStackSize(8 * 1024 * 1024);
#endif
    worker_thread->start();
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
    QMetaObject::invokeMethod(m_state->worker, [worker = m_state->worker, id, work = std::move(work)] {
        worker->runTask(id, work);
    }, Qt::QueuedConnection);
    return true;
}

BackendExecutor::TaskId BackendExecutor::enqueueTask(std::function<void(bool)> work, std::function<void(std::optional<Cancelled>)> gui_completion)
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
    m_state->pending.emplace(id, std::move(gui_completion));
    QMetaObject::invokeMethod(m_state->worker, [worker = m_state->worker, id, work = std::move(work)] {
        worker->runTask(id, [work] { work(true); });
    }, Qt::QueuedConnection);
    return id;
}

void BackendExecutor::shutdown()
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_stopping) return;
    m_stopping = true;
    qDebug() << __func__ << ": Stopping thread" << m_state->worker_thread;
    m_gui_completions.clear();
    disconnect(m_state->worker, nullptr, this, nullptr);
    // A completion can enter a nested event loop, so reaching the worker barrier
    // alone does not mean that GUI callbacks have released their task state.
    QMetaObject::invokeMethod(m_state->worker, [state = m_state] {
        QMetaObject::invokeMethod(QCoreApplication::instance(), [state] {
            state->worker_barrier = true;
            state->finishShutdownIfReady();
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

void BackendExecutor::shutdownAll(QObject* receiver, std::function<void()> done)
{
    Q_ASSERT(QThread::currentThread() == QCoreApplication::instance()->thread());
    QStringList pending_workers;
    for (const auto& [thread, owner] : GetExecutorRegistry().owners_by_thread) {
        pending_workers.push_back(thread->objectName());
    }
    qDebug() << __func__ << ": Waiting for executors" << pending_workers << "and pending thread joins";
    GetExecutorRegistry().drain_callbacks.emplace_back(receiver, [done = std::move(done)] {
        qDebug() << "All tracked background workers drained";
        done();
    });
    for (const auto& [thread, owner] : GetExecutorRegistry().owners_by_thread) {
        if (owner) owner->shutdown();
    }
    GetExecutorRegistry().notifyIfDrained();
}

#include <qml/backendexecutor.moc>

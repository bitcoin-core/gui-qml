// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/backendexecutor.h>
#include <qml/asyncjoin.h>

#include <QCoreApplication>
#include <QMetaObject>

#include <vector>
#include <QThread>

namespace {
// GUI-affine registry. Entries survive the executor QObject because accepted
// jobs may own values after their originating view has been destroyed.
struct ExecutorRegistry {
    std::map<QThread*, QPointer<BackendExecutor>> workers;
    std::vector<std::pair<QPointer<QObject>, std::function<void()>>> completions;

    void notifyIfDrained()
    {
        if (!workers.empty()) return;
        auto callbacks = std::move(completions);
        completions.clear();
        for (auto& [receiver, done] : callbacks) {
            if (receiver) WaitForThreadJoins(receiver, std::move(done));
        }
    }
};

ExecutorRegistry& Registry()
{
    static ExecutorRegistry registry;
    return registry;
}
} // namespace

class BackendExecutorWorker : public QObject
{
    Q_OBJECT
public:
    void run(quint64 id, std::function<void()> work)
    {
        // Keep task/results owned by the worker until the GUI acknowledges the
        // completion, or shutdown discards it. A canceled owning result must
        // not destroy its backend handle on the GUI thread.
        m_tasks.emplace(id, std::move(work));
        m_tasks.at(id)();
        Q_EMIT completed(id);
    }
    void release(quint64 id) { m_tasks.erase(id); }
    void clear() { m_tasks.clear(); }
Q_SIGNALS:
    void completed(quint64 id);
private:
    std::map<quint64, std::function<void()>> m_tasks;
};

BackendExecutor::BackendExecutor(QObject* parent)
    : QObject(parent), m_worker(new BackendExecutorWorker), m_thread(new QThread)
{
    Q_ASSERT(QCoreApplication::instance());
    Q_ASSERT(QThread::currentThread() == QCoreApplication::instance()->thread());
    Registry().workers.emplace(m_thread, this);
    connect(m_thread, &QThread::finished, QCoreApplication::instance(), [thread = m_thread, owner = QPointer<BackendExecutor>{this}] {
        JoinThreadAsync(thread, QCoreApplication::instance(), [thread, owner] {
            Registry().workers.erase(thread);
            if (owner) {
                owner->m_drained = true;
                owner->m_worker = nullptr;
                owner->m_thread = nullptr;
                Q_EMIT owner->drained();
            }
            Registry().notifyIfDrained();
        });
    }, Qt::QueuedConnection);
    m_worker->moveToThread(m_thread);
    connect(m_worker, &BackendExecutorWorker::completed, this, [this](quint64 id) {
        const auto it = m_completions.find(id);
        if (it == m_completions.end()) return;
        auto completion = std::move(it->second);
        m_completions.erase(it);
        const QPointer<BackendExecutor> guard{this};
        completion();
        completion = {};
        if (!guard || m_stopping) return;
        QMetaObject::invokeMethod(m_worker, [worker = m_worker, id] { worker->release(id); }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread->start();
}

BackendExecutor::~BackendExecutor()
{
    shutdown();
}

bool BackendExecutor::enqueue(std::function<void()> work, std::function<void()> completion)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_stopping) return false;
    const quint64 id{++m_next_id};
    m_completions.emplace(id, std::move(completion));
    QMetaObject::invokeMethod(m_worker, [worker = m_worker, id, work = std::move(work)] {
        worker->run(id, work);
    }, Qt::QueuedConnection);
    return true;
}

void BackendExecutor::shutdown()
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_stopping) return;
    m_stopping = true;
    m_completions.clear();
    // Disconnect while the worker is still alive. Once the quit barrier is
    // posted, worker destruction can race the owner's QObject destructor,
    // which otherwise calls back into the sender to remove this connection.
    disconnect(m_worker, nullptr, this, nullptr);
    // A queued barrier preserves command order and keeps the worker/context
    // alive until all accepted jobs have released their backend references.
    QMetaObject::invokeMethod(m_worker, [worker = m_worker, thread = m_thread] {
        worker->clear();
        // Let the GUI finish posting this barrier before stopping the event
        // dispatcher it wakes. Otherwise a fast worker can tear down that
        // dispatcher while the posting thread is still inside postEvent().
        QMetaObject::invokeMethod(thread, &QThread::quit, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

void BackendExecutor::shutdownAll(QObject* receiver, std::function<void()> done)
{
    Q_ASSERT(QThread::currentThread() == QCoreApplication::instance()->thread());
    Registry().completions.emplace_back(receiver, std::move(done));
    for (const auto& [thread, owner] : Registry().workers) {
        if (owner) owner->shutdown();
    }
    Registry().notifyIfDrained();
}

#include <qml/backendexecutor.moc>

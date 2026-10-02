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

BackendExecutor::BackendExecutor(QObject* parent)
    : QObject(parent), m_worker(new BackendExecutorWorker), m_thread(new QThread)
{
    Q_ASSERT(QCoreApplication::instance());
    Q_ASSERT(QThread::currentThread() == QCoreApplication::instance()->thread());
    GetExecutorRegistry().owners_by_thread.emplace(m_thread, this);
    connect(m_thread, &QThread::finished, QCoreApplication::instance(), [thread = m_thread, owner = QPointer<BackendExecutor>{this}] {
        JoinThreadAsync(thread, QCoreApplication::instance(), [thread, owner] {
            GetExecutorRegistry().owners_by_thread.erase(thread);
            if (owner) {
                owner->m_drained = true;
                owner->m_worker = nullptr;
                owner->m_thread = nullptr;
                Q_EMIT owner->drained();
            }
            GetExecutorRegistry().notifyIfDrained();
        });
    }, Qt::QueuedConnection);
    m_worker->moveToThread(m_thread);
    connect(m_worker, &BackendExecutorWorker::completed, this, [this](quint64 id) {
        const auto it = m_gui_completions.find(id);
        if (it == m_gui_completions.end()) return;
        auto gui_completion = std::move(it->second);
        m_gui_completions.erase(it);
        const QPointer<BackendExecutor> owner_guard{this};
        gui_completion();
        gui_completion = {};
        if (!owner_guard || m_stopping) return;
        QMetaObject::invokeMethod(m_worker, [worker = m_worker, id] { worker->releaseTask(id); }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread->start();
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

void BackendExecutor::shutdown()
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_stopping) return;
    m_stopping = true;
    m_gui_completions.clear();
    disconnect(m_worker, nullptr, this, nullptr);
    QMetaObject::invokeMethod(m_worker, [worker = m_worker, gui_owned_thread = m_thread] {
        worker->releaseAllTasks();
        QMetaObject::invokeMethod(gui_owned_thread, &QThread::quit, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

void BackendExecutor::shutdownAll(QObject* receiver, std::function<void()> done)
{
    Q_ASSERT(QThread::currentThread() == QCoreApplication::instance()->thread());
    GetExecutorRegistry().drain_callbacks.emplace_back(receiver, std::move(done));
    for (const auto& [thread, owner] : GetExecutorRegistry().owners_by_thread) {
        if (owner) owner->shutdown();
    }
    GetExecutorRegistry().notifyIfDrained();
}

#include <qml/backendexecutor.moc>

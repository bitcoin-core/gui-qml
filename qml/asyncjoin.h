// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_ASYNCJOIN_H
#define BITCOIN_QML_ASYNCJOIN_H

#include <QCoreApplication>
#include <QDebug>
#include <QMetaObject>
#include <QPointer>
#include <QThread>

#include <functional>
#include <future>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

namespace async_join_detail {
struct ThreadJoinRegistry {
    int pending_joins{0};
    std::vector<std::pair<QPointer<QObject>, std::function<void()>>> drain_callbacks;

    void notifyIfDrained()
    {
        if (pending_joins) return;
        auto ready_callbacks = std::move(drain_callbacks);
        drain_callbacks.clear();
        for (auto& [receiver, done] : ready_callbacks) {
            if (receiver) QMetaObject::invokeMethod(receiver, std::move(done), Qt::QueuedConnection);
        }
    }
};

inline ThreadJoinRegistry& GetRegistry()
{
    static ThreadJoinRegistry registry;
    return registry;
}
}

inline void WaitForThreadJoins(QObject* receiver, std::function<void()> done)
{
    Q_ASSERT(QThread::currentThread() == QCoreApplication::instance()->thread());
    auto& registry = async_join_detail::GetRegistry();
    registry.drain_callbacks.emplace_back(receiver, std::move(done));
    registry.notifyIfDrained();
}

inline void JoinThreadAsync(std::function<void()> join, QObject* receiver, std::function<void()> done)
{
    Q_ASSERT(QThread::currentThread() == QCoreApplication::instance()->thread());
    ++async_join_detail::GetRegistry().pending_joins;
    std::thread([join = std::move(join), app = QCoreApplication::instance(),
                 receiver_guard = QPointer<QObject>{receiver}, done = std::move(done)]() mutable {
        join();
        QMetaObject::invokeMethod(app, [receiver_guard, done = std::move(done)] {
            --async_join_detail::GetRegistry().pending_joins;
            if (receiver_guard) done();
            async_join_detail::GetRegistry().notifyIfDrained();
        }, Qt::QueuedConnection);
    }).detach();
}

// Register the join now so application teardown can see it. The returned
// callback releases the join once the worker's owners have drained.
inline std::function<void()> JoinThreadAfterDrain(std::function<void()> join, QObject* receiver, std::function<void()> done)
{
    const auto drained = std::make_shared<std::promise<void>>();
    auto ready = drained->get_future().share();
    JoinThreadAsync([join = std::move(join), ready = std::move(ready)] {
        ready.wait();
        join();
    }, receiver, std::move(done));
    return [drained] { drained->set_value(); };
}

inline void JoinThreadAsync(QThread* worker_thread, QObject* receiver, std::function<void()> done)
{
    Q_ASSERT(QThread::currentThread() == QCoreApplication::instance()->thread());
    worker_thread->setParent(nullptr);
    qDebug() << __func__ << ": Joining thread" << worker_thread;
    ++async_join_detail::GetRegistry().pending_joins;
    std::thread([worker_thread, app = QCoreApplication::instance(), receiver_guard = QPointer<QObject>{receiver}, done = std::move(done)]() mutable {
        if (!worker_thread->wait()) return;
        QMetaObject::invokeMethod(app, [worker_thread, receiver_guard, done = std::move(done)] {
            qDebug() << "JoinThreadAsync: Stopped thread" << worker_thread;
            delete worker_thread;
            --async_join_detail::GetRegistry().pending_joins;
            if (receiver_guard) done();
            async_join_detail::GetRegistry().notifyIfDrained();
        }, Qt::QueuedConnection);
    }).detach();
}

#endif // BITCOIN_QML_ASYNCJOIN_H

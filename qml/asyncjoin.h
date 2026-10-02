// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_ASYNCJOIN_H
#define BITCOIN_QML_ASYNCJOIN_H

#include <QCoreApplication>
#include <QPointer>
#include <QThread>
#include <QTimer>

#include <atomic>
#include <functional>
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

inline void JoinThreadAsync(QThread* worker_thread, QObject* receiver, std::function<void()> done)
{
    Q_ASSERT(QThread::currentThread() == QCoreApplication::instance()->thread());
    worker_thread->setParent(nullptr);
    ++async_join_detail::GetRegistry().pending_joins;
    auto thread_joined = std::make_shared<std::atomic_bool>(false);
    std::thread([worker_thread, thread_joined] {
        const bool join_succeeded{worker_thread->wait()};
        thread_joined->store(join_succeeded, std::memory_order_release);
    }).detach();

    auto* join_poll_timer = new QTimer{QCoreApplication::instance()};
    QObject::connect(join_poll_timer, &QTimer::timeout, join_poll_timer,
        [worker_thread, thread_joined, join_poll_timer, receiver_guard = QPointer<QObject>{receiver}, done = std::move(done)] {
            if (!thread_joined->load(std::memory_order_acquire)) return;
            join_poll_timer->stop();
            join_poll_timer->deleteLater();
            delete worker_thread;
            --async_join_detail::GetRegistry().pending_joins;
            if (receiver_guard) done();
            async_join_detail::GetRegistry().notifyIfDrained();
        });
    join_poll_timer->start(1);
}

#endif // BITCOIN_QML_ASYNCJOIN_H

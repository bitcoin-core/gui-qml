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
struct Registry {
    int pending{0};
    std::vector<std::pair<QPointer<QObject>, std::function<void()>>> completions;

    void notifyIfDrained()
    {
        if (pending) return;
        auto callbacks = std::move(completions);
        completions.clear();
        for (auto& [receiver, done] : callbacks) {
            if (receiver) QMetaObject::invokeMethod(receiver, std::move(done), Qt::QueuedConnection);
        }
    }
};

inline Registry& GetRegistry()
{
    static Registry registry;
    return registry;
}
} // namespace async_join_detail

/** Final application-thread barrier, including joins whose owner was deleted. */
inline void WaitForThreadJoins(QObject* receiver, std::function<void()> done)
{
    Q_ASSERT(QThread::currentThread() == QCoreApplication::instance()->thread());
    auto& registry = async_join_detail::GetRegistry();
    registry.completions.emplace_back(receiver, std::move(done));
    registry.notifyIfDrained();
}

/** Take ownership of an exiting thread and delete it after its final cleanup.
 * Call on the application thread, keep its event loop alive until done, and do
 * not otherwise delete worker_thread. The completion runs on the application
 * thread while receiver survives. Unlike finished(), it includes deferred
 * deletion and thread-local cleanup. No join blocks the application thread.
 */
inline void JoinThreadAsync(QThread* worker_thread, QObject* receiver, std::function<void()> done)
{
    Q_ASSERT(QThread::currentThread() == QCoreApplication::instance()->thread());
    worker_thread->setParent(nullptr);
    ++async_join_detail::GetRegistry().pending;
    auto joined = std::make_shared<std::atomic_bool>(false);
    std::thread([worker_thread, joined] {
        const bool finished{worker_thread->wait()};
        // The join helper makes no Qt calls after publishing completion. In
        // particular, it must not post an event that races application exit.
        joined->store(finished, std::memory_order_release);
    }).detach();

    auto* timer = new QTimer{QCoreApplication::instance()};
    QObject::connect(timer, &QTimer::timeout, timer,
        [worker_thread, joined, timer, guard = QPointer<QObject>{receiver}, done = std::move(done)] {
            if (!joined->load(std::memory_order_acquire)) return;
            timer->stop();
            timer->deleteLater();
            delete worker_thread;
            --async_join_detail::GetRegistry().pending;
            if (guard) done();
            async_join_detail::GetRegistry().notifyIfDrained();
        });
    timer->start(1);
}

#endif // BITCOIN_QML_ASYNCJOIN_H

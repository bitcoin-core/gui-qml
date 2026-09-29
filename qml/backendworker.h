// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_BACKENDWORKER_H
#define BITCOIN_QML_BACKENDWORKER_H

#include <QMetaObject>
#include <QObject>
#include <QThread>

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <utility>

// A serial backend queue. Results always return to the owner's thread. drain()
// closes admission and asynchronously waits for accepted work before emitting
// drained(); callers must keep backend dependencies alive until that signal.
class BackendWorker : public QObject
{
    Q_OBJECT
public:
    explicit BackendWorker(QObject* parent = nullptr);
    ~BackendWorker() override;

    template <typename Work, typename Apply>
    void submit(Work work, Apply apply)
    {
        requireOwnerThread();
        if (m_stopping) return;
        QMetaObject::invokeMethod(m_context, [this, work = std::move(work), apply = std::move(apply)]() mutable {
            auto result = work();
            QMetaObject::invokeMethod(this, [this, result = std::move(result), apply = std::move(apply)]() mutable {
                requireOwnerThread();
                if (!m_stopping) apply(std::move(result));
            }, Qt::QueuedConnection);
        }, Qt::QueuedConnection);
    }

    void post(std::function<void()> work);
    void drain();
    void requireOwnerThread() const;

Q_SIGNALS:
    void drained();

private:
    QThread m_thread;
    QObject* m_context;
    bool m_stopping{false};
};

// Unconditional: Release integration tests must also catch worker mutations.
inline void RequireModelThread(const QObject* model)
{
    if (QThread::currentThread() != model->thread()) {
        std::fprintf(stderr, "GUI model mutated on a foreign thread: %s\n", model->metaObject()->className());
        std::fflush(stderr);
        std::abort();
    }
}

#endif // BITCOIN_QML_BACKENDWORKER_H

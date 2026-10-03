// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_BACKENDEXECUTOR_H
#define BITCOIN_QML_BACKENDEXECUTOR_H

#include <QObject>
#include <QPointer>

#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

class QThread;
class BackendExecutorWorker;

/** A GUI-owned serial backend queue. Jobs must own the backend state they use.
 *
 * Results are delivered on the GUI thread only while the receiver is alive.
 * shutdown() rejects new jobs, discards GUI completions, and drains accepted
 * work without waiting on the GUI. The owner must wait for drained() before
 * destroying backend state borrowed by jobs. Destruction also requests a drain
 * but does not wait: jobs that can outlive the owner must capture shared state.
 */
class BackendExecutor : public QObject
{
    Q_OBJECT
public:
    explicit BackendExecutor(QObject* parent = nullptr);
    ~BackendExecutor() override;

    /** Called on the GUI thread. Work runs off-thread; done and failed run on GUI.
     * A false return means shutdown has begun and the job was not accepted.
     * The result must be an owned value, never a reference into backend state.
     */
    template <typename Work, typename Done>
    bool submit(QObject* receiver, Work work, Done done,
                std::function<void(std::exception_ptr)> failed = {})
    {
        using Return = std::invoke_result_t<Work>;
        static_assert(!std::is_reference_v<Return>, "Return an owned backend result");
        using Result = std::conditional_t<std::is_void_v<Return>, std::monostate, Return>;
        struct Task {
            std::optional<Work> work;
            std::optional<Result> result;
            std::exception_ptr error;
        };
        auto task = std::make_shared<Task>(Task{std::move(work), {}, {}});
        auto completion = std::make_shared<Done>(std::move(done));
        const QPointer<QObject> guard{receiver};
        return enqueue([task] {
            try {
                if constexpr (std::is_void_v<Return>) {
                    std::invoke(*task->work);
                    task->result.emplace();
                } else {
                    task->result.emplace(std::invoke(*task->work));
                }
            } catch (...) {
                task->error = std::current_exception();
            }
            // Release backend captures on the worker, before publishing values.
            task->work.reset();
        }, [task, completion, guard, failed = std::move(failed)] {
            if (!guard) return;
            if (task->error) {
                if (failed) failed(task->error);
                return;
            }
            if constexpr (std::is_void_v<Return>) {
                std::invoke(*completion);
            } else {
                std::invoke(*completion, std::move(*task->result));
            }
        });
    }

    void shutdown();
    /** Final application-exit barrier for remaining value-owning jobs (for
     * example exports whose QML view has gone away). Every worker borrowing
     * Node or wallet state must already have drained before backend shutdown.
     * Call on the application thread, after producers have stopped.
     */
    static void shutdownAll(QObject* receiver, std::function<void()> done);
    bool isDrained() const { return m_drained; }

Q_SIGNALS:
    void drained();

private:
    bool enqueue(std::function<void()> work, std::function<void()> completion);
    BackendExecutorWorker* m_worker;
    QThread* m_thread;
    std::map<quint64, std::function<void()>> m_completions;
    quint64 m_next_id{0};
    bool m_stopping{false};
    bool m_drained{false};
};

#endif // BITCOIN_QML_BACKENDEXECUTOR_H

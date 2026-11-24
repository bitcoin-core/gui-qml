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

class BackendExecutor : public QObject
{
    Q_OBJECT
public:
    using TaskId = quint64;
    enum class CancelPolicy { QueuedOnly, DiscardRunningResult };
    struct Cancelled {
        // Rejected means the submission arrived after shutdown began.
        enum class Reason { Requested, Rejected };
        Reason reason;
    };
    template <typename T>
    using Outcome = std::variant<std::conditional_t<std::is_void_v<T>, std::monostate, T>, std::exception_ptr, Cancelled>;

    explicit BackendExecutor(QObject* parent = nullptr);
    ~BackendExecutor() override;

    template <typename Work, typename Done>
    bool submit(QObject* receiver, Work work, Done done,
                std::function<void(std::exception_ptr)> failed = {})
    {
        using Return = std::invoke_result_t<Work>;
        auto task = std::make_shared<Task<Work>>(Task<Work>{std::move(work), {}, {}});
        auto gui_completion = std::make_shared<Done>(std::move(done));
        const QPointer<QObject> receiver_guard{receiver};
        return enqueue([task] { task->run(true); }, [task, gui_completion, receiver_guard, failed = std::move(failed)] {
            if (!receiver_guard) return;
            if (task->error) {
                if (failed) failed(task->error);
                return;
            }
            if constexpr (std::is_void_v<Return>) {
                std::invoke(*gui_completion);
            } else {
                std::invoke(*gui_completion, std::move(*task->result));
            }
        });
    }

    // Task IDs belong to this executor. A non-null GUI-thread receiver is required.
    // Completion is queued once on the GUI thread, including rejection, and
    // survives executor destruction while receiver lives.
    // Accepted work releases its captures and discarded results on the worker;
    // rejected work releases its captures on the calling thread.
    // Completion callbacks must not throw.
    template <typename Work, typename Done>
    TaskId submitTask(QObject* receiver, Work work, Done done, CancelPolicy policy = CancelPolicy::QueuedOnly)
    {
        using Return = std::invoke_result_t<Work>;
        Q_ASSERT(receiver && receiver->thread() == thread());
        auto task = std::make_shared<Task<Work>>(Task<Work>{std::move(work), {}, {}});
        auto gui_completion = std::make_shared<Done>(std::move(done));
        const QPointer<QObject> receiver_guard{receiver};
        return enqueueTask([task](bool run) { task->run(run); }, [task, gui_completion, receiver_guard](std::optional<Cancelled> cancelled) {
            if (!receiver_guard) return;
            if (cancelled) {
                std::invoke(*gui_completion, Outcome<Return>{std::in_place_index<2>, *cancelled});
            } else if (task->error) {
                std::invoke(*gui_completion, Outcome<Return>{std::in_place_index<1>, task->error});
            } else {
                std::invoke(*gui_completion, Outcome<Return>{std::in_place_index<0>, std::move(*task->result)});
            }
        }, policy);
    }

    // Returns true only for a newly accepted cancellation. Queued work reports
    // Cancelled promptly; its captures may remain alive until the worker skips it.
    // Running work is never interrupted; DiscardRunningResult delivers Cancelled
    // after the work returns. Draining waits for all worker cleanup.
    bool cancel(TaskId id);
    // Always queued, including when already drained. Registration survives
    // executor destruction while receiver lives.
    void whenDrained(QObject* receiver, std::function<void()> done);
    // Drains accepted work without cancelling it, including submitTask completions
    // and worker cleanup. Rejection callbacks are queued independently and do not
    // delay draining.
    void shutdown();
    static void shutdownAll(QObject* receiver, std::function<void()> done);
    bool isDrained() const;

Q_SIGNALS:
    void drained();

private:
    template <typename Work>
    struct Task {
        using Return = std::invoke_result_t<Work>;
        static_assert(!std::is_reference_v<Return>, "Return an owned backend result");
        using Result = std::conditional_t<std::is_void_v<Return>, std::monostate, Return>;
        std::optional<Work> backend_work;
        std::optional<Result> result;
        std::exception_ptr error;

        void run(bool execute)
        {
            if (execute) {
                try {
                    if constexpr (std::is_void_v<Return>) {
                        std::invoke(*backend_work);
                        result.emplace();
                    } else {
                        result.emplace(std::invoke(*backend_work));
                    }
                } catch (...) {
                    error = std::current_exception();
                }
            }
            backend_work.reset();
        }
    };

    struct ExecutorState;
    TaskId enqueueTask(std::function<void(bool)> work, std::function<void(std::optional<Cancelled>)> gui_completion, CancelPolicy policy);
    bool enqueue(std::function<void()> work, std::function<void()> gui_completion);
    std::shared_ptr<ExecutorState> m_state;
    std::map<quint64, std::function<void()>> m_gui_completions;
    quint64 m_next_task_id{0};
    bool m_stopping{false};
};

#endif // BITCOIN_QML_BACKENDEXECUTOR_H

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

class BackendExecutor : public QObject
{
    Q_OBJECT
public:
    explicit BackendExecutor(QObject* parent = nullptr);
    ~BackendExecutor() override;

    template <typename Work, typename Done>
    bool submit(QObject* receiver, Work work, Done done,
                std::function<void(std::exception_ptr)> failed = {})
    {
        using Return = std::invoke_result_t<Work>;
        static_assert(!std::is_reference_v<Return>, "Return an owned backend result");
        using Result = std::conditional_t<std::is_void_v<Return>, std::monostate, Return>;
        struct Task {
            std::optional<Work> backend_work;
            std::optional<Result> result;
            std::exception_ptr error;
        };
        auto task = std::make_shared<Task>(Task{std::move(work), {}, {}});
        auto gui_completion = std::make_shared<Done>(std::move(done));
        const QPointer<QObject> receiver_guard{receiver};
        return enqueue([task] {
            try {
                if constexpr (std::is_void_v<Return>) {
                    std::invoke(*task->backend_work);
                    task->result.emplace();
                } else {
                    task->result.emplace(std::invoke(*task->backend_work));
                }
            } catch (...) {
                task->error = std::current_exception();
            }
            task->backend_work.reset();
        }, [task, gui_completion, receiver_guard, failed = std::move(failed)] {
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

    void shutdown();
    static void shutdownAll(QObject* receiver, std::function<void()> done);
    bool isDrained() const { return m_drained; }

Q_SIGNALS:
    void drained();

private:
    bool enqueue(std::function<void()> work, std::function<void()> gui_completion);
    BackendExecutorWorker* m_worker;
    QThread* m_thread;
    std::map<quint64, std::function<void()>> m_gui_completions;
    quint64 m_next_task_id{0};
    bool m_stopping{false};
    bool m_drained{false};
};

#endif // BITCOIN_QML_BACKENDEXECUTOR_H

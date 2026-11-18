// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/executor.h>

#include <qml/execution.h>

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace execution {
namespace {
using Callback = std::function<void()>;

class Worker
{
public:
    Worker() : m_thread([this] { run(); }) {}
    ~Worker()
    {
        close();
        join();
    }

    bool post(Callback callback)
    {
        {
            std::lock_guard lock{m_mutex};
            if (m_closed) return false;
            m_tasks.push_back(std::move(callback));
        }
        m_ready.notify_one();
        return true;
    }

    void close()
    {
        {
            std::lock_guard lock{m_mutex};
            m_closed = true;
        }
        m_ready.notify_one();
    }

    void join()
    {
        if (m_thread.joinable()) m_thread.join();
    }

private:
    void run()
    {
        for (;;) {
            Callback callback;
            {
                std::unique_lock lock{m_mutex};
                m_ready.wait(lock, [this] { return m_closed || !m_tasks.empty(); });
                if (m_tasks.empty()) return;
                callback.swap(m_tasks.front());
                m_tasks.pop_front();
            }
            callback();
        }
    }

    std::mutex m_mutex;
    std::condition_variable m_ready;
    std::deque<Callback> m_tasks;
    bool m_closed{false};
    std::thread m_thread;
};

struct Workers {
    Worker core, priority, rpc, io, signer, cleanup;

    bool post(Lane lane, UtilityWorker utility, Callback callback)
    {
        switch (lane) {
        case Lane::Core: return core.post(std::move(callback));
        case Lane::Priority: return priority.post(std::move(callback));
        case Lane::Rpc: return rpc.post(std::move(callback));
        case Lane::Utility:
            switch (utility) {
            case UtilityWorker::IO: return io.post(std::move(callback));
            case UtilityWorker::Signer: return signer.post(std::move(callback));
            case UtilityWorker::Cleanup: return cleanup.post(std::move(callback));
            }
            return false;
        case Lane::Domain:
        case Lane::Control:
        case Lane::Cleanup: return false;
        }
        return false;
    }

    void stopAndJoin()
    {
        for (auto* worker : {&core, &priority, &rpc, &io, &signer, &cleanup}) worker->close();
        for (auto* worker : {&core, &priority, &rpc, &io, &signer, &cleanup}) worker->join();
    }
};
} // namespace

struct SerialQueue::State : std::enable_shared_from_this<SerialQueue::State> {
    State(Executor::Dispatch dispatch, Lane lane, UtilityWorker utility)
        : dispatch(std::move(dispatch)), lane(lane), utility(utility) {}

    void run()
    {
        {
            std::lock_guard lock{mutex};
            admission = nullptr;
        }
        changed.notify_all();

        for (;;) {
            Callback task;
            std::vector<Callback> ready;
            {
                std::lock_guard lock{mutex};
                if (tasks.empty()) {
                    active = false;
                    ready = takeListeners();
                } else {
                    task.swap(tasks.front());
                    tasks.pop_front();
                }
            }
            if (!task) {
                changed.notify_all();
                notify(std::move(ready));
                return;
            }
            // Destroy captures before the next idle check, outside the mutex.
            task();
        }
    }

    void notify(std::vector<Callback> ready)
    {
        if (ready.empty()) return;
        // An idle queue can finish while some other queue is holding its lane.
        const auto deliver = [ready = std::move(ready)] {
            for (const auto& callback : ready) callback();
        };
        bool accepted{false};
        try {
            accepted = dispatch(Lane::Utility, UtilityWorker::IO, deliver);
        } catch (...) {
            // A transport failure still leaves this drain receipt deliverable.
        }
        if (!accepted) deliver();
    }

    // Requires mutex. A rejected task's captures may themselves post work.
    std::vector<Callback> takeListeners()
    {
        std::vector<Callback> ready;
        if (closed && !active && retiring == 0) ready.swap(listeners);
        return ready;
    }

    bool post(Callback task)
    {
        if (!task) return false;
        const char attempt{};
        {
            std::unique_lock lock{mutex};
            // Do not accept a second task until dispatch accepts the first.
            // run() releases the wait before invoking work for inline dispatch.
            changed.wait(lock, [this] { return closed || !admission; });
            if (closed) return false;
            tasks.push_back(std::move(task));
            if (active) return true;
            active = true;
            admission = &attempt;
        }

        bool accepted{false};
        try {
            accepted = dispatch(lane, utility, [self = shared_from_this()] { self->run(); });
        } catch (...) {
            // A failed submission follows the same rollback as rejection.
        }
        if (accepted) {
            {
                std::lock_guard lock{mutex};
                if (admission == &attempt) admission = nullptr;
            }
            changed.notify_all();
            return true;
        }

        {
            std::lock_guard lock{mutex};
            task = std::move(tasks.front());
            tasks.pop_front();
            active = false;
            admission = nullptr;
            ++retiring;
        }
        changed.notify_all();
        task = {};

        std::vector<Callback> ready;
        {
            std::lock_guard lock{mutex};
            --retiring;
            ready = takeListeners();
        }
        changed.notify_all();
        notify(std::move(ready));
        return false;
    }

    void close()
    {
        std::vector<Callback> ready;
        {
            std::lock_guard lock{mutex};
            closed = true;
            ready = takeListeners();
        }
        changed.notify_all();
        notify(std::move(ready));
    }

    void whenDrained(Callback done)
    {
        if (!done) return;
        {
            std::lock_guard lock{mutex};
            if (!closed || active || retiring != 0) {
                listeners.push_back(std::move(done));
                return;
            }
        }
        notify({std::move(done)});
    }

    void waitIdle()
    {
        std::unique_lock lock{mutex};
        changed.wait(lock, [this] { return !active && retiring == 0; });
    }

    const Executor::Dispatch dispatch;
    const Lane lane;
    const UtilityWorker utility;
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<Callback> tasks;
    std::vector<Callback> listeners;
    bool closed{false};
    bool active{false};
    const char* admission{nullptr};
    unsigned retiring{0};
};

SerialQueue::SerialQueue(std::shared_ptr<State> state) : m_state(std::move(state)) {}
bool SerialQueue::post(Callback task)
{
    const auto state = m_state;
    return state && state->post(std::move(task));
}

void SerialQueue::close()
{
    if (const auto state = m_state) state->close();
}
void SerialQueue::whenDrained(Callback done)
{
    if (const auto state = m_state) state->whenDrained(std::move(done));
}

struct Executor::State {
    std::shared_ptr<Workers> workers;
    Dispatch dispatch;
    std::mutex mutex;
    bool stopped{false};
    std::vector<std::weak_ptr<SerialQueue::State>> queues;
};

Executor::Executor(Config) : m_state(std::make_unique<State>())
{
    m_state->workers = std::make_shared<Workers>();
    m_state->dispatch = [workers = m_state->workers](Lane lane, UtilityWorker utility, Callback callback) {
        return workers->post(lane, utility, std::move(callback));
    };
}

Executor::Executor(Dispatch dispatch) : m_state(std::make_unique<State>())
{
    m_state->dispatch = std::move(dispatch);
}

Executor::~Executor() { stopAndJoin(); }

SerialQueue Executor::makeQueue(Lane lane, UtilityWorker utility)
{
    auto queue = std::make_shared<SerialQueue::State>(m_state->dispatch, lane, utility);
    std::lock_guard lock{m_state->mutex};
    if (m_state->stopped) queue->close();
    else {
        std::erase_if(m_state->queues, [](const auto& entry) { return entry.expired(); });
        m_state->queues.push_back(queue);
    }
    return SerialQueue{std::move(queue)};
}

void Executor::stopAndJoin()
{
    std::vector<std::weak_ptr<SerialQueue::State>> queues;
    {
        std::lock_guard lock{m_state->mutex};
        if (m_state->stopped) return;
        m_state->stopped = true;
        queues.swap(m_state->queues);
    }
    for (const auto& entry : queues) {
        if (const auto queue = entry.lock()) queue->close();
    }
    for (const auto& entry : queues) {
        if (const auto queue = entry.lock()) queue->waitIdle();
    }
    if (m_state->workers) m_state->workers->stopAndJoin();
}
} // namespace execution

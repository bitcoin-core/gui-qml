// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/retainedvalue.h>

#include <qml/executor.h>

#include <cassert>
#include <exception>

namespace execution {
ValueCleanupQueue::ValueCleanupQueue(std::shared_ptr<SerialQueue> queue)
    : m_queue(std::move(queue))
{
    assert(m_queue);
}

struct ValueCleanupQueue::Hold {
    std::shared_ptr<ValueCleanupQueue> owner;
    std::function<void()> cleanup;

    ~Hold()
    {
        if (!owner) return;
        if (!owner->m_queue->postReservedCleanup([owner = owner, cleanup = std::move(cleanup)]() mutable {
            try { cleanup(); } catch (...) {}
            cleanup = {};
            owner->release();
        })) std::terminate();
    }
};

std::shared_ptr<void> ValueCleanupQueue::hold(std::function<void()> cleanup)
{
    auto hold = std::make_shared<Hold>();
    hold->cleanup = std::move(cleanup);
    std::lock_guard lock{m_mutex};
    if (m_closed || !m_queue->reserveCleanup()) return {};
    hold->owner = shared_from_this();
    ++m_held;
    return hold;
}

void ValueCleanupQueue::release()
{
    bool drained;
    {
        std::lock_guard lock{m_mutex};
        drained = --m_held == 0 && m_closed;
    }
    if (drained) m_queue->close();
}

void ValueCleanupQueue::close(std::function<void()> done)
{
    bool drained;
    {
        std::lock_guard lock{m_mutex};
        m_closed = true;
        drained = m_held == 0;
    }
    if (drained) m_queue->close();
    m_queue->whenDrained(std::move(done));
}

SubmitResult ValueCleanupQueue::postWithOutcome(SerialQueue& worker, std::shared_ptr<Task> task)
{
    auto completed = std::make_shared<std::shared_ptr<Task>>();
    auto delivery = std::static_pointer_cast<Hold>(hold([completed] {
        if (auto outcome = std::move(*completed)) outcome->deliver();
    }));
    if (!delivery) return {{}, Rejection::Closed};
    const bool accepted = worker.post([task = std::move(task), completed, delivery]() mutable {
        task->execute();
        *completed = std::move(task);
    });
    if (!accepted) {
        // Core never ran. Consume the unused reservation even if the dispatcher
        // also refuses Utility work. There is no completed task to retire.
        delivery->owner.reset();
        m_queue->cancelReservedCleanup();
        release();
        return {{}, Rejection::Transport};
    }
    return {{}, Rejection::None};
}
} // namespace execution

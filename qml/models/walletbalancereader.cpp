// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/walletbalancereader.h>
#include <qml/executor.h>
#include <qml/retainedvalue.h>

#include <utility>

namespace walletbalance {
Reader::Drain::~Drain()
{
    if (notify) notify();
}

Reader::Reader(Post post, Read read, Notify notify,
               std::shared_ptr<std::atomic_bool> closed, Notify drained)
    : m_post(std::move(post)), m_read(std::move(read)), m_notify(std::move(notify)),
      m_closed(std::move(closed)),
      m_drain(drained ? std::make_shared<Drain>(std::move(drained)) : nullptr)
{
}

Reader::Reader(std::shared_ptr<execution::SerialQueue> core, std::shared_ptr<execution::SerialQueue> outcomes,
               Read read, Notify notify, std::shared_ptr<std::atomic_bool> closed, Notify drained)
    : Reader([core = std::move(core), delivery = std::make_shared<execution::ValueCleanupQueue>(std::move(outcomes))](std::shared_ptr<execution::Task> task) {
        return delivery->postWithOutcome(*core, std::move(task));
    }, std::move(read), std::move(notify), std::move(closed), std::move(drained))
{
}

execution::Rejection Reader::refresh()
{
    std::shared_ptr<Drain> drain;
    {
        std::lock_guard lock{m_mutex};
        if (m_closed->load()) return execution::Rejection::Closed;
        m_dirty = true;
        if (m_active) return execution::Rejection::None;
        m_active = true;
        drain = m_drain;
    }
    return submitReserved(std::move(drain));
}

execution::Rejection Reader::submitReserved(std::shared_ptr<Drain> drain)
{
    const auto self = shared_from_this();
    const auto attempt = std::make_shared<Attempt>(Attempt{false, std::move(drain)});
    auto read = [self, attempt] {
        {
            std::lock_guard lock{self->m_mutex};
            attempt->started = true;
            self->m_dirty = false;
        }
        if (self->m_closed->load()) return Result{Status::Closed, {}};
        return self->m_read();
    };
    auto complete = [self, attempt](Result result) {
        self->complete(attempt, std::move(result));
    };
    const auto rejection = m_post(execution::MakeTask(std::move(read), std::move(complete),
        [self, attempt](std::exception_ptr) {
            self->complete(attempt, {attempt->started ? Status::Failed : Status::Rejected, {}});
        })).rejection;
    if (rejection != execution::Rejection::None) {
        publish({Status::Rejected, {}});
        {
            std::lock_guard lock{m_mutex};
            m_active = false;
        }
    }
    return rejection;
}

void Reader::complete(const std::shared_ptr<Attempt>& attempt, Result result)
{
    bool repeat{false};
    std::shared_ptr<Drain> drain;
    // Keep admission occupied until this outcome has been published. A later
    // rejection cannot overtake it, including a reentrant refresh from Notify.
    publish(std::move(result));
    {
        std::lock_guard lock{m_mutex};
        m_active = false;
        if (m_closed->load()) return;
        // A rejected dispatch retains refresh interest, but never spins on a
        // saturated/closed transport. The next timer or notice retries it.
        repeat = attempt->started && m_dirty;
        if (repeat) {
            m_active = true;
            drain = m_drain;
        }
    }
    if (repeat) submitReserved(std::move(drain));
}

void Reader::publish(Result result)
{
    bool notify{false};
    {
        std::lock_guard lock{m_mutex};
        if (m_closed->load()) return;
        if (result.snapshot) m_last_snapshot = result.snapshot;
        else result.snapshot = m_last_snapshot;
        m_latest = std::move(result);
        notify = !std::exchange(m_notification_pending, true);
    }
    if (notify) {
        try { m_notify(); } catch (...) {
            std::lock_guard lock{m_mutex};
            m_notification_pending = false;
        }
    }
}

std::optional<Result> Reader::take()
{
    std::lock_guard lock{m_mutex};
    m_notification_pending = false;
    auto latest = std::exchange(m_latest, {});
    if (m_closed->load()) return {};
    return latest;
}

void Reader::close()
{
    m_closed->store(true);
    std::shared_ptr<Drain> drain;
    {
        std::lock_guard lock{m_mutex};
        m_latest.reset();
        m_last_snapshot.reset();
        m_dirty = false;
        drain = std::move(m_drain);
    }
    // A release notification may reenter; never invoke it under m_mutex.
    drain.reset();
}
} // namespace walletbalance

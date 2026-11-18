// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/walletbalancereader.h>

#include <utility>

namespace walletbalance {
Reader::Drain::~Drain()
{
    if (notify) notify();
}

Reader::Reader(std::shared_ptr<execution::Scheduler> scheduler, Read read, Notify notify,
               std::shared_ptr<std::atomic_bool> closed, Notify drained)
    : m_scheduler(std::move(scheduler)), m_read(std::move(read)), m_notify(std::move(notify)),
      m_closed(std::move(closed)), m_key(m_scheduler->allocateKey()),
      m_drain(std::make_shared<Drain>(std::move(drained)))
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
    const auto admission = execution::Submit(*m_scheduler, [self, attempt] {
        {
            std::lock_guard lock{self->m_mutex};
            attempt->started = true;
            self->m_dirty = false;
        }
        if (self->m_closed->load()) return Result{Status::Closed, {}};
        return self->m_read();
    }, [self, attempt](Result result) {
        self->complete(attempt, std::move(result));
    }, [self, attempt](std::exception_ptr) {
        self->complete(attempt, {attempt->started ? Status::Failed : Status::Rejected, {}});
    }, {execution::Lane::Core, m_key, execution::WorkClass::Observation});
    if (!admission) {
        publish({Status::Rejected, {}});
        {
            std::lock_guard lock{m_mutex};
            m_active = false;
        }
    }
    return admission.rejection;
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

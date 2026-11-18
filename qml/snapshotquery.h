// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_SNAPSHOTQUERY_H
#define BITCOIN_QML_SNAPSHOTQUERY_H

#include <qml/execution.h>

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

namespace execution {
/** Accepted effects own their outcome independently of GUI delivery. Dropping
 * the observer neither cancels the effect nor rewrites a completed mutation. */
template <typename Value>
class OwnedCommand final : public std::enable_shared_from_this<OwnedCommand<Value>>
{
public:
    struct Result { std::optional<Value> value; std::exception_ptr error; bool publication_failed{false}; };
    explicit OwnedCommand(std::function<void()> notify)
        : m_notify(std::move(notify)) {}
    template <typename Starter>
    SubmitResult startWith(Starter starter)
    {
        {
            std::lock_guard lock{m_mutex};
            if (std::exchange(m_started, true)) return {{}, Rejection::Invalid};
        }
        const auto self = this->shared_from_this();
        return starter(
            [self](Value value) { self->complete({std::move(value), {}}); },
            [self](std::exception_ptr error) { self->complete({{}, error}); });
    }
    std::optional<Result> result() const
    {
        std::lock_guard lock{m_mutex};
        return m_result;
    }
private:
    void complete(Result result)
    {
        {
            std::lock_guard lock{m_mutex};
            if (m_result) return;
            m_result = std::move(result);
        }
        // Publication failure cannot erase or re-execute the actual effect.
        try { m_notify(); } catch (...) {
            std::lock_guard lock{m_mutex};
            m_result->publication_failed = true;
        }
    }
    const std::function<void()> m_notify;
    mutable std::mutex m_mutex;
    bool m_started{false};
    std::optional<Result> m_result;
};
/** A coalesced observation with Core work and Domain outcome ownership.
 * Notify only enqueues a presentation turn. A failed read retains the last
 * successful snapshot in the facade; it never masquerades as an empty read.
 * Requests arriving during a read cause one follow-up, without an idle loop.
 */
template <typename Snapshot>
class SnapshotQuery final : public std::enable_shared_from_this<SnapshotQuery<Snapshot>>
{
public:
    struct Result {
        std::shared_ptr<const Snapshot> snapshot;
        std::exception_ptr error;
    };
    using Read = std::function<Snapshot()>;
    using Post = std::function<SubmitResult(std::shared_ptr<Task>)>;
    SnapshotQuery(Read read, std::function<void()> notify, Post post)
        : m_post(std::move(post)), m_read(std::move(read)), m_notify(std::move(notify)) {}

    void request()
    {
        {
            std::lock_guard lock{m_mutex};
            if (m_closed) return;
            if (m_running) { m_dirty = true; return; }
            m_running = true;
        }
        const auto self = this->shared_from_this();
        const auto submitted = m_post(MakeTask([self] {
            {
                std::lock_guard lock{self->m_mutex};
                if (self->m_closed) throw NotStarted{NotStarted::Cause::Cancelled};
            }
            return std::make_shared<const Snapshot>(self->m_read());
        }, [self](std::shared_ptr<const Snapshot> snapshot) { self->complete({std::move(snapshot), {}}); },
        [self](std::exception_ptr error) { self->complete({{}, error}); }));
        // Requests coalesced during a rejected admission share that failure.
        // Retrying inline cannot restore transport capacity and could recurse
        // indefinitely while another caller keeps the observation dirty.
        if (!submitted) recordResult({{}, std::make_exception_ptr(NotStarted{NotStarted::Cause::Transport})});
    }

    std::optional<Result> take()
    {
        std::lock_guard lock{m_mutex};
        m_notice_pending = false;
        return std::exchange(m_latest, {});
    }

    /** Drop only an unvalued observation failure before requesting a newer
     * origin. Context-bearing success/failure snapshots remain available to
     * their normal revision fence. This API never owns action outcomes. */
    void discardReadFailure()
    {
        std::lock_guard lock{m_mutex};
        if (m_latest && m_latest->error && !m_latest->snapshot) {
            m_latest.reset(); m_notice_pending = false;
        }
    }

    void close()
    {
        std::lock_guard lock{m_mutex};
        m_closed = true;
        m_dirty = false;
    }

private:
    void complete(Result result)
    {
        if (recordResult(std::move(result))) request();
    }
    bool recordResult(Result result)
    {
        bool notify{false}, again{false};
        {
            std::lock_guard lock{m_mutex};
            m_running = false;
            if (m_closed) return false;
            m_latest = std::move(result);
            notify = !std::exchange(m_notice_pending, true);
            again = std::exchange(m_dirty, false);
        }
        if (notify) {
            try { m_notify(); } catch (...) {
                std::lock_guard lock{m_mutex};
                m_notice_pending = false;
            }
        }
        return again;
    }
    const Post m_post;
    const Read m_read;
    const std::function<void()> m_notify;
    std::mutex m_mutex;
    bool m_closed{false}, m_running{false}, m_dirty{false}, m_notice_pending{false};
    std::optional<Result> m_latest;
};
} // namespace execution

#endif // BITCOIN_QML_SNAPSHOTQUERY_H

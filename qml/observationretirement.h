// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_OBSERVATIONRETIREMENT_H
#define BITCOIN_QML_OBSERVATIONRETIREMENT_H

#include <qml/execution.h>

#include <array>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>

namespace execution {
/** Return obsolete presentation references to the existing Domain executor.
 * Releases neither data nor callbacks under the ingress lock. Coalesced turns
 * release at most eight references, then yield to other Domain work. Payload
 * construction/destruction still needs its own size/incremental-work policy.
 * A rejected post retains data for retry or the owner's background teardown.
 */
template <typename Snapshot>
class ObservationRetirement
{
    using Post = std::function<Admission(std::function<void()>)>;
    struct State : std::enable_shared_from_this<State> {
        explicit State(Post post) : post(std::move(post)) {}
        const Post post;
        std::mutex mutex;
        std::deque<std::shared_ptr<const Snapshot>> pending;
        bool scheduled{false};
        void schedule()
        {
            {
                std::lock_guard lock{mutex};
                if (scheduled || pending.empty()) return;
                scheduled = true;
            }
            const auto self = this->shared_from_this();
            if (!post([self] { self->release(); })) {
                std::lock_guard lock{mutex};
                scheduled = false;
            }
        }
        void release()
        {
            std::array<std::shared_ptr<const Snapshot>, 8> batch;
            {
                std::lock_guard lock{mutex};
                for (auto& item : batch) {
                    if (pending.empty()) break;
                    item = std::move(pending.front()); pending.pop_front();
                }
                scheduled = false;
            }
            // In particular, last-reference destruction must not reenter a
            // producer while its publication or retirement mutex is held.
            batch = {};
            schedule();
        }
    };
    const std::shared_ptr<State> m_state;
public:
    explicit ObservationRetirement(Post post) : m_state(std::make_shared<State>(std::move(post))) {}
    void retire(std::shared_ptr<const Snapshot> snapshot) const
    {
        if (!snapshot) return;
        {
            std::lock_guard lock{m_state->mutex};
            m_state->pending.push_back(std::move(snapshot));
        }
        retry();
    }
    size_t pending() const
    {
        std::lock_guard lock{m_state->mutex};
        return m_state->pending.size();
    }
    void retry() const { m_state->schedule(); }
};
} // namespace execution

#endif // BITCOIN_QML_OBSERVATIONRETIREMENT_H
